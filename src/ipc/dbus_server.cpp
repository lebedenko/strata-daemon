#include "ipc/dbus_server.hpp"

#include <cstring>
#include <nlohmann/json.hpp>
#include <systemd/sd-bus.h>

#include "common/logger.hpp"

namespace strata {

static const sd_bus_vtable strata_device_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetStatus", "", "s", &DBusServer::method_get_status, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetLayers", "", "s", &DBusServer::method_get_layers, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetKeymap", "ub", "s", &DBusServer::method_get_keymap,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RefreshKeymap", "", "b", &DBusServer::method_refresh_keymap,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ClearCache", "", "b", &DBusServer::method_clear_cache,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("LayerChanged", "usus", 0),
    SD_BUS_SIGNAL("DeviceConnected", "sss", 0),
    SD_BUS_SIGNAL("DeviceDisconnected", "s", 0),
    SD_BUS_SIGNAL("KeymapLoaded", "ssu", 0),
    SD_BUS_SIGNAL("LayerBindingsLoaded", "uu", 0),
    SD_BUS_VTABLE_END};

DBusServer::DBusServer(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

DBusServer::~DBusServer() {
    close();
}

DBusServer::DBusServer(DBusServer &&other) noexcept
    : callbacks_(std::move(other.callbacks_))
    , bus_(other.bus_)
    , slot_(other.slot_) {
    other.bus_ = nullptr;
    other.slot_ = nullptr;
}

DBusServer &DBusServer::operator=(DBusServer &&other) noexcept {
    if (this != &other) {
        close();
        callbacks_ = std::move(other.callbacks_);
        bus_ = other.bus_;
        slot_ = other.slot_;
        other.bus_ = nullptr;
        other.slot_ = nullptr;
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

    r = sd_bus_add_object_vtable(bus_, &slot_, OBJECT_PATH.data(), INTERFACE_NAME.data(),
                                 strata_device_vtable, this);
    if (r < 0) {
        LOG_ERROR("Failed to register D-Bus vtable on {}: {}", OBJECT_PATH, strerror(-r));
        close();
        return false;
    }

    r = sd_bus_request_name(bus_, SERVICE_NAME.data(), SD_BUS_NAME_REPLACE_EXISTING);
    if (r < 0) {
        LOG_ERROR("Failed to request D-Bus well-known name {}: {}", SERVICE_NAME, strerror(-r));
        close();
        return false;
    }

    LOG_INFO("Registered D-Bus service '{}' at object path '{}'", SERVICE_NAME, OBJECT_PATH);
    return true;
}

void DBusServer::close() {
    if (slot_) {
        sd_bus_slot_unref(slot_);
        slot_ = nullptr;
    }
    if (bus_) {
        sd_bus_flush_close_unref(bus_);
        bus_ = nullptr;
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

void DBusServer::emit_layer_changed(uint32_t index, std::string_view name, uint32_t mask,
                                    std::string_view build_id) {
    if (!bus_)
        return;
    std::string name_str(name);
    std::string build_str(build_id);
    sd_bus_emit_signal(bus_, OBJECT_PATH.data(), INTERFACE_NAME.data(), "LayerChanged", "usus",
                       index, name_str.c_str(), mask, build_str.c_str());
}

void DBusServer::emit_device_connected(std::string_view name, std::string_view node,
                                       std::string_view build_id) {
    if (!bus_)
        return;
    std::string name_str(name);
    std::string node_str(node);
    std::string build_str(build_id);
    sd_bus_emit_signal(bus_, OBJECT_PATH.data(), INTERFACE_NAME.data(), "DeviceConnected", "sss",
                       name_str.c_str(), node_str.c_str(), build_str.c_str());
}

void DBusServer::emit_device_disconnected(std::string_view node) {
    if (!bus_)
        return;
    std::string node_str(node);
    sd_bus_emit_signal(bus_, OBJECT_PATH.data(), INTERFACE_NAME.data(), "DeviceDisconnected", "s",
                       node_str.c_str());
}

void DBusServer::emit_keymap_loaded(std::string_view build_id, std::string_view source,
                                    uint32_t layer_count) {
    if (!bus_)
        return;
    std::string build_str(build_id);
    std::string source_str(source);
    sd_bus_emit_signal(bus_, OBJECT_PATH.data(), INTERFACE_NAME.data(), "KeymapLoaded", "ssu",
                       build_str.c_str(), source_str.c_str(), layer_count);
}

void DBusServer::emit_layer_bindings_loaded(uint32_t layer, uint32_t count) {
    if (!bus_)
        return;
    sd_bus_emit_signal(bus_, OBJECT_PATH.data(), INTERFACE_NAME.data(), "LayerBindingsLoaded", "uu",
                       layer, count);
}

int DBusServer::method_get_status(sd_bus_message *m, void *userdata,
                                  [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    DeviceStatus st{};
    if (self->callbacks_.get_status) {
        st = self->callbacks_.get_status();
    }

    nlohmann::ordered_json j;
    j["connected"] = st.connected;
    j["name"] = st.name;
    j["node"] = st.node;
    j["build_id"] = st.buildId;
    j["active_layer"] = {
        {"index", st.activeLayerIndex}, {"name", st.activeLayerName}, {"mask", st.activeLayerMask}};
    j["layers_count"] = st.layersCount;
    j["cached"] = st.cached;

    std::string s = j.dump();
    return sd_bus_reply_method_return(m, "s", s.c_str());
}

int DBusServer::method_get_layers(sd_bus_message *m, void *userdata,
                                  [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    std::vector<LayerInfo> layers;
    if (self->callbacks_.get_layers) {
        layers = self->callbacks_.get_layers();
    }

    nlohmann::ordered_json j = nlohmann::ordered_json::array();
    for (const auto &l : layers) {
        j.push_back(nlohmann::ordered_json::object(
            {{"index", l.index}, {"id", l.id}, {"name", l.name}, {"active", l.isActive}}));
    }

    std::string s = j.dump();
    return sd_bus_reply_method_return(m, "s", s.c_str());
}

int DBusServer::method_get_keymap(sd_bus_message *m, void *userdata,
                                  [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    uint32_t layer_idx = 255;
    int refresh = 0;
    int r = sd_bus_message_read(m, "ub", &layer_idx, &refresh);
    if (r < 0) {
        return sd_bus_reply_method_errorf(m, "org.freedesktop.Strata.Error.InvalidArgs",
                                          "Expected (ub)");
    }

    if (refresh && self->callbacks_.refresh_keymap) {
        self->callbacks_.refresh_keymap();
    }

    std::optional<KeymapData> data;
    if (self->callbacks_.get_keymap) {
        data = self->callbacks_.get_keymap(layer_idx);
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
            b_arr.push_back(nlohmann::ordered_json::object({{"pos", b.pos},
                                                            {"behavior", b.behavior},
                                                            {"param1", b.param1},
                                                            {"param2", b.param2}}));
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

int DBusServer::method_refresh_keymap(sd_bus_message *m, void *userdata,
                                      [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    bool ok = false;
    if (self->callbacks_.refresh_keymap) {
        ok = self->callbacks_.refresh_keymap();
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

int DBusServer::method_clear_cache(sd_bus_message *m, void *userdata,
                                   [[maybe_unused]] sd_bus_error *ret_error) {
    auto *self = static_cast<DBusServer *>(userdata);
    bool ok = false;
    if (self->callbacks_.clear_cache) {
        ok = self->callbacks_.clear_cache();
    }
    return sd_bus_reply_method_return(m, "b", ok ? 1 : 0);
}

} // namespace strata
