#include "ipc/dbus_server.hpp"

#include <cstring>
#include <nlohmann/json.hpp>
#include <systemd/sd-bus.h>

#include "common/logger.hpp"

namespace strata {

namespace {

const sd_bus_vtable manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetDevices", "", "ao", &DBusServer::method_manager_get_devices,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetActiveDevice", "", "o", &DBusServer::method_manager_get_active_device,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetActiveDevice", "s", "b", &DBusServer::method_manager_set_active_device,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("DeviceAdded", "os", 0),
    SD_BUS_SIGNAL("DeviceRemoved", "os", 0),
    SD_BUS_SIGNAL("ActiveDeviceChanged", "os", 0),
    SD_BUS_VTABLE_END};

const sd_bus_vtable device_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetStatus", "", "s", &DBusServer::method_device_get_status,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetLayer", "ub", "b", &DBusServer::method_device_set_layer,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetRgbControl", "b", "b", &DBusServer::method_device_set_rgb_control,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetRgbLed", "yyyy", "b", &DBusServer::method_device_set_rgb_led,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetRgbAll", "yyy", "b", &DBusServer::method_device_set_rgb_all,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("UpdateBrightness", "b", "b", &DBusServer::method_device_update_brightness,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("LayerChanged", "usus", 0),
    SD_BUS_SIGNAL("KeyEvent", "yyb", 0),
    SD_BUS_VTABLE_END};

const sd_bus_vtable keymap_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetLayers", "", "s", &DBusServer::method_keymap_get_layers,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetKeymap", "ub", "s", &DBusServer::method_keymap_get_keymap,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RefreshKeymap", "", "b", &DBusServer::method_keymap_refresh,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ClearCache", "", "b", &DBusServer::method_keymap_clear_cache,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("KeymapLoaded", "ssu", 0),
    SD_BUS_SIGNAL("LayerBindingsLoaded", "uu", 0),
    SD_BUS_VTABLE_END};

std::string extract_device_id_from_msg(sd_bus_message *m, DBusServer *server = nullptr) {
    const char *path = sd_bus_message_get_path(m);
    if (!path) {
        return "";
    }
    if (server) {
        auto it = server->device_path_to_id().find(path);
        if (it != server->device_path_to_id().end()) {
            return it->second;
        }
    }
    std::string_view p(path);
    if (p.starts_with(DBusServer::DEVICE_BASE_PATH)) {
        auto sub = p.substr(DBusServer::DEVICE_BASE_PATH.size());
        if (sub.starts_with("/")) {
            sub = sub.substr(1);
        }
        return std::string(sub);
    }
    return "";
}

} // namespace

std::string DBusServer::sanitize_id(std::string_view id) {
    std::string s;
    s.reserve(id.size());
    for (char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_') {
            s.push_back(c);
        } else {
            s.push_back('_');
        }
    }
    return s.empty() ? "device" : s;
}

std::string DBusServer::device_path(std::string_view id) {
    return std::string(DEVICE_BASE_PATH) + "/" + sanitize_id(id);
}

DBusServer::DBusServer(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

DBusServer::~DBusServer() {
    close();
}

DBusServer::DBusServer(DBusServer &&other) noexcept
    : callbacks_(std::move(other.callbacks_))
    , bus_(other.bus_)
    , manager_slot_(other.manager_slot_)
    , device_slots_(std::move(other.device_slots_)) {
    other.bus_ = nullptr;
    other.manager_slot_ = nullptr;
}

DBusServer &DBusServer::operator=(DBusServer &&other) noexcept {
    if (this != &other) {
        close();
        callbacks_ = std::move(other.callbacks_);
        bus_ = other.bus_;
        manager_slot_ = other.manager_slot_;
        device_slots_ = std::move(other.device_slots_);
        other.bus_ = nullptr;
        other.manager_slot_ = nullptr;
    }
    return *this;
}

bool DBusServer::init() {
    int r = sd_bus_open_user(&bus_);
    if (r < 0) {
        LOG_WARN("sd_bus_open_user failed ({}), attempting sd_bus_default_user...", strerror(-r));
        r = sd_bus_default_user(&bus_);
    }
    if (r < 0) {
        LOG_ERROR("Failed to connect to user session bus: {}", strerror(-r));
        return false;
    }

    // 1. Register Manager object
    r = sd_bus_add_object_vtable(bus_, &manager_slot_, MANAGER_PATH.data(),
                                 MANAGER_INTERFACE.data(), manager_vtable, this);
    if (r < 0) {
        LOG_ERROR("Failed to register Manager vtable on {}: {}", MANAGER_PATH, strerror(-r));
        close();
        return false;
    }

    // 2. Request service name: io.github.lebedenko.Strata
    r = sd_bus_request_name(bus_, SERVICE_NAME.data(), SD_BUS_NAME_REPLACE_EXISTING);
    if (r < 0) {
        LOG_ERROR("Failed to request D-Bus name {}: {}", SERVICE_NAME, strerror(-r));
        close();
        return false;
    }

    LOG_INFO("Registered D-Bus service '{}' with Manager at '{}'", SERVICE_NAME, MANAGER_PATH);
    return true;
}

void DBusServer::close() {
    for (auto &[id, slots] : device_slots_) {
        for (auto *slot : slots) {
            sd_bus_slot_unref(slot);
        }
    }
    device_slots_.clear();
    device_path_to_id_.clear();

    if (manager_slot_) {
        sd_bus_slot_unref(manager_slot_);
        manager_slot_ = nullptr;
    }
    if (bus_) {
        sd_bus_flush_close_unref(bus_);
        bus_ = nullptr;
    }
}

void DBusServer::register_device(device::IDevice *device) {
    if (!bus_ || !device) {
        return;
    }

    std::string id = device->id();
    std::string path = device_path(id);

    // Unregister if already present
    unregister_device(id);

    std::vector<sd_bus_slot *> slots;

    // Register Device1 interface
    sd_bus_slot *dSlot = nullptr;
    int r = sd_bus_add_object_vtable(bus_, &dSlot, path.c_str(), DEVICE_INTERFACE.data(),
                                     device_vtable, this);
    if (r >= 0 && dSlot) {
        slots.push_back(dSlot);
    }

    // Register Keymap1 interface if device has readable keymap
    if (device->hasCapability(DeviceCapability::ReadableKeymap)) {
        sd_bus_slot *kSlot = nullptr;
        r = sd_bus_add_object_vtable(bus_, &kSlot, path.c_str(), KEYMAP_INTERFACE.data(),
                                     keymap_vtable, this);
        if (r >= 0 && kSlot) {
            slots.push_back(kSlot);
        }
    }

    device_slots_[id] = std::move(slots);
    device_path_to_id_[path] = id;

    LOG_INFO("Exposed device '{}' at D-Bus object path '{}'", id, path);
    sd_bus_emit_signal(bus_, MANAGER_PATH.data(), MANAGER_INTERFACE.data(), "DeviceAdded", "os",
                       path.c_str(), id.c_str());
}

void DBusServer::unregister_device(const std::string &id) {
    auto it = device_slots_.find(id);
    if (it != device_slots_.end()) {
        for (auto *slot : it->second) {
            sd_bus_slot_unref(slot);
        }
        device_slots_.erase(it);

        std::string path = device_path(id);
        device_path_to_id_.erase(path);
        if (bus_) {
            sd_bus_emit_signal(bus_, MANAGER_PATH.data(), MANAGER_INTERFACE.data(), "DeviceRemoved",
                               "os", path.c_str(), id.c_str());
        }
        LOG_INFO("Removed device '{}' from D-Bus", id);
    }
}

int DBusServer::get_fd() const {
    return bus_ ? sd_bus_get_fd(bus_) : -1;
}

short DBusServer::get_events() const {
    if (!bus_)
        return 0;
    int ev = sd_bus_get_events(bus_);
    return ev > 0 ? static_cast<short>(ev) : 0;
}

uint64_t DBusServer::get_timeout_usec() const {
    if (!bus_)
        return UINT64_MAX;
    uint64_t usec = UINT64_MAX;
    sd_bus_get_timeout(bus_, &usec);
    return usec;
}

int DBusServer::process() {
    if (!bus_)
        return 0;
    int r = 0;
    while ((r = sd_bus_process(bus_, nullptr)) > 0) {
        // Drain processed messages
    }
    return r;
}

void DBusServer::emit_layer_changed(const std::string &deviceId, uint32_t index,
                                    std::string_view name, uint32_t mask,
                                    std::string_view build_id) {
    if (!bus_)
        return;
    std::string name_str(name);
    std::string build_str(build_id);

    // Emit on per-device object path
    std::string devPath = device_path(deviceId);
    sd_bus_emit_signal(bus_, devPath.c_str(), DEVICE_INTERFACE.data(), "LayerChanged", "usus",
                       index, name_str.c_str(), mask, build_str.c_str());
}

void DBusServer::emit_active_device_changed(std::string_view id, std::string_view path) {
    if (!bus_)
        return;
    std::string id_str(id);
    std::string path_str(path);
    sd_bus_emit_signal(bus_, MANAGER_PATH.data(), MANAGER_INTERFACE.data(), "ActiveDeviceChanged",
                       "os", path_str.c_str(), id_str.c_str());
}

void DBusServer::emit_keymap_loaded(const std::string &deviceId, std::string_view build_id,
                                    std::string_view source, uint32_t layer_count) {
    if (!bus_)
        return;
    std::string build_str(build_id);
    std::string source_str(source);
    std::string devPath = device_path(deviceId);

    sd_bus_emit_signal(bus_, devPath.c_str(), KEYMAP_INTERFACE.data(), "KeymapLoaded", "ssu",
                       build_str.c_str(), source_str.c_str(), layer_count);
}

void DBusServer::emit_layer_bindings_loaded(const std::string &deviceId, uint32_t layer,
                                            uint32_t count) {
    if (!bus_)
        return;
    std::string devPath = device_path(deviceId);
    sd_bus_emit_signal(bus_, devPath.c_str(), KEYMAP_INTERFACE.data(), "LayerBindingsLoaded", "uu",
                       layer, count);
}

void DBusServer::emit_key_event(const std::string &deviceId, uint8_t col, uint8_t row,
                                bool pressed) {
    if (!bus_)
        return;
    std::string devPath = device_path(deviceId);
    sd_bus_emit_signal(bus_, devPath.c_str(), DEVICE_INTERFACE.data(), "KeyEvent", "yyb", col, row,
                       pressed ? 1 : 0);
}

// Manager methods
int DBusServer::method_manager_get_devices(sd_bus_message *m, void *userdata,
                                           [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::vector<device::IDevice *> devs;
    if (self->callbacks_.get_devices) {
        devs = self->callbacks_.get_devices();
    }

    sd_bus_message *reply = nullptr;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0)
        return r;

    r = sd_bus_message_open_container(reply, 'a', "o");
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    for (const auto *dev : devs) {
        if (dev) {
            std::string path = device_path(dev->id());
            r = sd_bus_message_append(reply, "o", path.c_str());
            if (r < 0) {
                sd_bus_message_unref(reply);
                return r;
            }
        }
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    return sd_bus_send(self->bus_, reply, nullptr);
}

int DBusServer::method_manager_get_active_device(sd_bus_message *m, void *userdata,
                                                 [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string path = "/";
    if (self->callbacks_.get_active_device) {
        auto *dev = self->callbacks_.get_active_device();
        if (dev) {
            path = device_path(dev->id());
        }
    }
    return sd_bus_reply_method_return(m, "o", path.c_str());
}

int DBusServer::method_manager_set_active_device(sd_bus_message *m, void *userdata,
                                                 [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    const char *id = nullptr;
    int r = sd_bus_message_read(m, "s", &id);
    if (r < 0 || !id) {
        return sd_bus_reply_method_errorf(m, "io.github.lebedenko.Strata.Error.InvalidArgs",
                                          "Expected string device id");
    }

    std::string targetId = id;
    auto it = self->device_path_to_id_.find(targetId);
    if (it != self->device_path_to_id_.end()) {
        targetId = it->second;
    } else if (targetId.starts_with(DEVICE_BASE_PATH)) {
        auto sub = std::string_view(targetId).substr(DEVICE_BASE_PATH.size());
        if (sub.starts_with("/")) {
            sub = sub.substr(1);
        }
        targetId = std::string(sub);
    }

    bool ok = false;
    if (self->callbacks_.set_active_device) {
        ok = self->callbacks_.set_active_device(targetId);
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

// Device methods
int DBusServer::method_device_get_status(sd_bus_message *m, void *userdata,
                                         [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    DeviceStatus st{};
    if (self->callbacks_.get_device_status) {
        st = self->callbacks_.get_device_status(devId);
    }

    nlohmann::ordered_json j;
    j["id"] = st.id.empty() ? devId : st.id;
    j["type"] = st.type;
    j["connected"] = st.connected;
    j["name"] = st.name;
    j["node"] = st.node;
    j["build_id"] = st.buildId;
    j["active_layer"] = {
        {"index", st.activeLayerIndex}, {"name", st.activeLayerName}, {"mask", st.activeLayerMask}};
    j["layers_count"] = st.layersCount;
    j["cached"] = st.cached;
    j["capabilities"] = st.capabilities;

    std::string s = j.dump();
    return sd_bus_reply_method_return(m, "s", s.c_str());
}

int DBusServer::method_device_set_layer(sd_bus_message *m, void *userdata,
                                        [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    uint32_t layer = 0;
    int lock = 1;
    int r = sd_bus_message_read(m, "ub", &layer, &lock);
    if (r < 0) {
        return sd_bus_reply_method_errorf(m, "io.github.lebedenko.Strata.Error.InvalidArgs",
                                          "Expected (ub)");
    }

    bool ok = false;
    if (self->callbacks_.set_layer) {
        ok = self->callbacks_.set_layer(devId, static_cast<uint8_t>(layer), lock != 0);
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

int DBusServer::method_device_set_rgb_control(sd_bus_message *m, void *userdata,
                                              [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    int enable = 0;
    int r = sd_bus_message_read(m, "b", &enable);
    if (r < 0) {
        return sd_bus_reply_method_errorf(m, "io.github.lebedenko.Strata.Error.InvalidArgs",
                                          "Expected (b)");
    }

    bool ok = false;
    if (self->callbacks_.set_rgb_control) {
        ok = self->callbacks_.set_rgb_control(devId, enable != 0);
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

int DBusServer::method_device_set_rgb_led(sd_bus_message *m, void *userdata,
                                          [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    uint8_t led = 0, red = 0, green = 0, blue = 0;
    int r = sd_bus_message_read(m, "yyyy", &led, &red, &green, &blue);
    if (r < 0) {
        return sd_bus_reply_method_errorf(m, "io.github.lebedenko.Strata.Error.InvalidArgs",
                                          "Expected (yyyy)");
    }

    bool ok = false;
    if (self->callbacks_.set_rgb_led) {
        ok = self->callbacks_.set_rgb_led(devId, led, red, green, blue);
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

int DBusServer::method_device_set_rgb_all(sd_bus_message *m, void *userdata,
                                          [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    uint8_t red = 0, green = 0, blue = 0;
    int r = sd_bus_message_read(m, "yyy", &red, &green, &blue);
    if (r < 0) {
        return sd_bus_reply_method_errorf(m, "io.github.lebedenko.Strata.Error.InvalidArgs",
                                          "Expected (yyy)");
    }

    bool ok = false;
    if (self->callbacks_.set_rgb_all) {
        ok = self->callbacks_.set_rgb_all(devId, red, green, blue);
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

int DBusServer::method_device_update_brightness(sd_bus_message *m, void *userdata,
                                                [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    int increase = 0;
    int r = sd_bus_message_read(m, "b", &increase);
    if (r < 0) {
        return sd_bus_reply_method_errorf(m, "io.github.lebedenko.Strata.Error.InvalidArgs",
                                          "Expected (b)");
    }

    bool ok = false;
    if (self->callbacks_.update_brightness) {
        ok = self->callbacks_.update_brightness(devId, increase != 0);
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

// Keymap methods
int DBusServer::method_keymap_get_layers(sd_bus_message *m, void *userdata,
                                         [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    std::vector<LayerInfo> layers;
    if (self->callbacks_.get_layers) {
        layers = self->callbacks_.get_layers(devId);
    }

    nlohmann::ordered_json j = nlohmann::ordered_json::array();
    for (const auto &l : layers) {
        j.push_back(nlohmann::ordered_json::object(
            {{"index", l.index}, {"id", l.id}, {"name", l.name}, {"active", l.isActive}}));
    }

    std::string s = j.dump();
    return sd_bus_reply_method_return(m, "s", s.c_str());
}

int DBusServer::method_keymap_get_keymap(sd_bus_message *m, void *userdata,
                                         [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);

    uint32_t layer_idx = 255;
    int refresh = 0;
    int r = sd_bus_message_read(m, "ub", &layer_idx, &refresh);
    if (r < 0) {
        return sd_bus_reply_method_errorf(m, "io.github.lebedenko.Strata.Error.InvalidArgs",
                                          "Expected (ub)");
    }

    if (refresh && self->callbacks_.refresh_keymap) {
        self->callbacks_.refresh_keymap(devId);
    }

    std::optional<KeymapData> data;
    if (self->callbacks_.get_keymap) {
        data = self->callbacks_.get_keymap(devId, layer_idx);
    }

    if (!data) {
        return sd_bus_reply_method_return(m, "s", "{}");
    }

    nlohmann::ordered_json j;
    j["build_id"] = data->buildId;
    j["device"] = data->deviceName;
    j["summary"] = {{"layer_count", data->summary.layerCount},
                    {"keys_per_layer", data->summary.keysPerLayer},
                    {"sensors_per_layer", data->summary.sensorsPerLayer},
                    {"default_layer", data->summary.defaultLayer},
                    {"build_id", data->summary.buildId}};

    nlohmann::ordered_json layers_arr = nlohmann::ordered_json::array();
    for (const auto &l : data->layers) {
        layers_arr.push_back(nlohmann::ordered_json::object(
            {{"index", l.index}, {"id", l.id}, {"name", l.name}, {"active", l.isActive}}));
    }
    j["layers"] = layers_arr;

    nlohmann::ordered_json bindings_obj = nlohmann::ordered_json::object();
    for (const auto &[idx, b_list] : data->bindings) {
        if (layer_idx != 255 && idx != layer_idx) {
            continue;
        }
        nlohmann::ordered_json b_arr = nlohmann::ordered_json::array();
        for (const auto &b : b_list) {
            nlohmann::ordered_json b_obj = {{"pos", b.pos},
                                            {"behavior", b.behavior},
                                            {"param1", b.param1},
                                            {"param2", b.param2}};
            if (!b.primaryLabel.empty())
                b_obj["primaryLabel"] = b.primaryLabel;
            if (!b.secondaryLabel.empty())
                b_obj["secondaryLabel"] = b.secondaryLabel;
            if (!b.category.empty())
                b_obj["category"] = b.category;
            if (!b.tooltip.empty())
                b_obj["tooltip"] = b.tooltip;
            b_arr.push_back(std::move(b_obj));
        }
        bindings_obj[std::to_string(idx)] = b_arr;
    }
    j["bindings"] = bindings_obj;

    nlohmann::ordered_json sensor_bindings_obj = nlohmann::ordered_json::object();
    for (const auto &[idx, s_list] : data->sensorBindings) {
        if (layer_idx != 255 && idx != layer_idx) {
            continue;
        }
        nlohmann::ordered_json s_arr = nlohmann::ordered_json::array();
        for (const auto &s : s_list) {
            s_arr.push_back(nlohmann::ordered_json::object({{"sensor", s.sensorIndex},
                                                            {"behavior", s.behavior},
                                                            {"param1", s.param1},
                                                            {"param2", s.param2}}));
        }
        sensor_bindings_obj[std::to_string(idx)] = s_arr;
    }
    j["sensor_bindings"] = sensor_bindings_obj;

    std::string s = j.dump();
    return sd_bus_reply_method_return(m, "s", s.c_str());
}

int DBusServer::method_keymap_refresh(sd_bus_message *m, void *userdata,
                                      [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::string devId = extract_device_id_from_msg(m, self);
    bool ok = false;
    if (self->callbacks_.refresh_keymap) {
        ok = self->callbacks_.refresh_keymap(devId);
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

int DBusServer::method_keymap_clear_cache([[maybe_unused]] sd_bus_message *m, void *userdata,
                                          [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    bool ok = false;
    if (self->callbacks_.clear_cache) {
        ok = self->callbacks_.clear_cache();
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

} // namespace strata
