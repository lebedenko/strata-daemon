#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <systemd/sd-bus.h>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "device/i_device.hpp"

namespace strata {

class DBusServer {
public:
    static constexpr std::string_view SERVICE_NAME = "io.github.lebedenko.Strata";
    static constexpr std::string_view MANAGER_PATH = "/io/github/lebedenko/Strata/Manager";
    static constexpr std::string_view MANAGER_INTERFACE = "io.github.lebedenko.Strata.Manager1";

    static constexpr std::string_view DEVICE_BASE_PATH = "/io/github/lebedenko/Strata/devices";
    static constexpr std::string_view DEVICE_INTERFACE = "io.github.lebedenko.Strata.Device1";
    static constexpr std::string_view KEYMAP_INTERFACE = "io.github.lebedenko.Strata.Keymap1";
    static constexpr std::string_view LAYER_CONTROL_INTERFACE =
        "io.github.lebedenko.Strata.LayerControl1";
    static constexpr std::string_view LIGHTING_INTERFACE = "io.github.lebedenko.Strata.Lighting1";

    struct Callbacks {
        std::function<std::vector<device::IDevice *>()> get_devices;
        std::function<device::IDevice *()> get_active_device;
        std::function<bool(const std::string &)> set_active_device;

        std::function<DeviceStatus(const std::string &)> get_device_status;
        std::function<std::vector<LayerInfo>(const std::string &)> get_layers;
        std::function<std::optional<KeymapData>(const std::string &, uint32_t)> get_keymap;
        std::function<bool(const std::string &)> refresh_keymap;
        std::function<bool()> clear_cache;

        // Device write commands
        std::function<bool(const std::string &, uint8_t, bool)> set_layer;
        std::function<bool(const std::string &, bool)> set_rgb_control;
        std::function<bool(const std::string &, uint8_t, uint8_t, uint8_t, uint8_t)> set_rgb_led;
        std::function<bool(const std::string &, uint8_t, uint8_t, uint8_t)> set_rgb_all;
        std::function<bool(const std::string &, bool)> update_brightness;
    };

    explicit DBusServer(Callbacks callbacks);
    ~DBusServer();

    DBusServer(const DBusServer &) = delete;
    DBusServer &operator=(const DBusServer &) = delete;
    DBusServer(DBusServer &&other) noexcept;
    DBusServer &operator=(DBusServer &&other) noexcept;

    bool init();
    void close();

    [[nodiscard]] int get_fd() const;
    [[nodiscard]] short get_events() const;
    [[nodiscard]] uint64_t get_timeout_usec() const;
    int process();

    // Device registration on D-Bus
    void register_device(device::IDevice *device);
    void unregister_device(const std::string &id);

    // Signal emitters
    void emit_layer_changed(const std::string &deviceId, uint32_t index, std::string_view name,
                            uint32_t mask, std::string_view build_id);

    void emit_active_device_changed(std::string_view id, std::string_view path);

    void emit_keymap_loaded(const std::string &deviceId, std::string_view build_id,
                            std::string_view source, uint32_t layer_count);

    void emit_layer_bindings_loaded(const std::string &deviceId, uint32_t layer, uint32_t count);

    void emit_key_event(const std::string &deviceId, uint8_t col, uint8_t row, bool pressed);

    [[nodiscard]] const Callbacks &callbacks() const noexcept { return callbacks_; }

    static std::string sanitize_id(std::string_view id);
    static std::string device_path(std::string_view id);

    // Manager D-Bus methods
    static int method_manager_get_devices(sd_bus_message *m, void *userdata,
                                          sd_bus_error *ret_error);
    static int method_manager_get_active_device(sd_bus_message *m, void *userdata,
                                                sd_bus_error *ret_error);
    static int method_manager_set_active_device(sd_bus_message *m, void *userdata,
                                                sd_bus_error *ret_error);

    // Device D-Bus methods
    static int method_device_get_status(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_device_set_layer(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_device_set_rgb_control(sd_bus_message *m, void *userdata,
                                             sd_bus_error *ret_error);
    static int method_device_set_rgb_led(sd_bus_message *m, void *userdata,
                                         sd_bus_error *ret_error);
    static int method_device_set_rgb_all(sd_bus_message *m, void *userdata,
                                         sd_bus_error *ret_error);
    static int method_device_update_brightness(sd_bus_message *m, void *userdata,
                                               sd_bus_error *ret_error);

    // Keymap D-Bus methods
    static int method_keymap_get_layers(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_keymap_get_keymap(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_keymap_refresh(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_keymap_clear_cache(sd_bus_message *m, void *userdata,
                                         sd_bus_error *ret_error);

    [[nodiscard]] const std::unordered_map<std::string, std::string> &
    device_path_to_id() const noexcept {
        return device_path_to_id_;
    }

private:
    Callbacks callbacks_;
    sd_bus *bus_{nullptr};
    sd_bus_slot *manager_slot_{nullptr};
    std::unordered_map<std::string, std::vector<sd_bus_slot *>> device_slots_;
    std::unordered_map<std::string, std::string> device_path_to_id_;
};

} // namespace strata
