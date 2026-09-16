#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <systemd/sd-bus.h>
#include <vector>

#include "common/types.hpp"

namespace strata {

class DBusServer {
public:
    static constexpr std::string_view SERVICE_NAME = "org.freedesktop.Strata";
    static constexpr std::string_view OBJECT_PATH = "/org/freedesktop/Strata/Device0";
    static constexpr std::string_view INTERFACE_NAME = "org.freedesktop.Strata.Device1";

    struct Callbacks {
        std::function<DeviceStatus()> get_status;
        std::function<std::vector<LayerInfo>()> get_layers;
        std::function<std::optional<KeymapData>(uint32_t)> get_keymap;
        std::function<bool()> refresh_keymap;
        std::function<bool()> clear_cache;
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

    // Signal emitters
    void emit_layer_changed(uint32_t index, std::string_view name, uint32_t mask,
                            std::string_view build_id);

    void emit_device_connected(std::string_view name, std::string_view node,
                               std::string_view build_id);

    void emit_device_disconnected(std::string_view node);

    void emit_keymap_loaded(std::string_view build_id, std::string_view source,
                            uint32_t layer_count);

    void emit_layer_bindings_loaded(uint32_t layer, uint32_t count);

    [[nodiscard]] const Callbacks &callbacks() const noexcept { return callbacks_; }

    // D-Bus method callbacks
    static int method_get_status(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_get_layers(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_get_keymap(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_refresh_keymap(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int method_clear_cache(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

private:
    Callbacks callbacks_;
    sd_bus *bus_{nullptr};
    sd_bus_slot *slot_{nullptr};
};

} // namespace strata
