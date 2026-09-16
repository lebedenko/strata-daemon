#include <csignal>
#include <cstdlib>
#include <format>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <systemd/sd-bus.h>

using json = nlohmann::ordered_json;

namespace {

static volatile sig_atomic_t g_running = 1;

void sigint_handler(int) {
    g_running = 0;
}

void print_help(std::string_view prog) {
    std::cout << "Strata CLI - Modern Control Tool for Strata Keyboard Daemon\n\n"
              << "Usage: " << prog << " <command> [options]\n\n"
              << "Commands:\n"
              << "  status                 Display current keyboard and daemon status\n"
              << "  layers                 List configured keyboard layers\n"
              << "  keymap                 Inspect key bindings for layers\n"
              << "  cache                  Inspect or clear keymap cache\n"
              << "  listen                 Stream real-time daemon events and layer transitions\n\n"
              << "Options:\n"
              << "  --json                 Format output as JSON\n"
              << "  --layer <N>            Target specific layer index (for 'keymap')\n"
              << "  --refresh              Force fresh query from hardware\n"
              << "  --clear                Clear keymap cache (for 'cache')\n"
              << "  -h, --help             Show this help message\n";
}

sd_bus *connect_bus() {
    sd_bus *bus = nullptr;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        r = sd_bus_default_user(&bus);
    }
    if (r < 0) {
        std::cerr << "Error: Cannot connect to D-Bus session bus (" << strerror(-r) << ")\n";
        return nullptr;
    }
    return bus;
}

int cmd_status(sd_bus *bus, bool as_json) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;

    int r = sd_bus_call_method(bus, "org.freedesktop.Strata", "/org/freedesktop/Strata/Device0",
                               "org.freedesktop.Strata.Device1", "GetStatus", &error, &reply, "");
    if (r < 0) {
        std::cerr << "Failed to query status from stratad: "
                  << (error.message ? error.message : strerror(-r)) << "\n";
        sd_bus_error_free(&error);
        return 1;
    }

    const char *json_raw = nullptr;
    sd_bus_message_read(reply, "s", &json_raw);
    json j = json::parse(json_raw ? json_raw : "{}");
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    if (as_json) {
        std::cout << j.dump(2) << "\n";
        return 0;
    }

    bool connected = j.value("connected", false);
    if (!connected) {
        std::cout << "Keyboard:     Disconnected\n";
        return 0;
    }

    std::string name = j.value("name", "Unknown");
    std::string node = j.value("node", "");
    std::string build_id = j.value("build_id", "Unknown");
    size_t layers_count = j.value("layers_count", 0);
    bool cached = j.value("cached", false);

    auto active = j.value("active_layer", json::object());
    uint32_t active_idx = active.value("index", 0);
    std::string active_name = active.value("name", "unknown");
    uint32_t active_mask = active.value("mask", 0);

    std::cout << std::format("Keyboard:     Connected ({}) [{}]\n"
                             "Build ID:     {}\n"
                             "Active Layer: {} (Index: {}, Mask: 0x{:08x})\n"
                             "Layers:       {} configured\n"
                             "Cache:        {}\n",
                             node, name,
                             build_id.empty() ? "None" : build_id,
                             active_name, active_idx, active_mask,
                             layers_count,
                             cached ? "Cached" : "Live Hardware");

    return 0;
}

int cmd_layers(sd_bus *bus, bool as_json) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;

    int r = sd_bus_call_method(bus,
                               "org.freedesktop.Strata",
                               "/org/freedesktop/Strata/Device0",
                               "org.freedesktop.Strata.Device1",
                               "GetLayers",
                               &error,
                               &reply,
                               "");
    if (r < 0) {
        std::cerr << "Failed to query layers: "
                  << (error.message ? error.message : strerror(-r)) << "\n";
        sd_bus_error_free(&error);
        return 1;
    }

    const char *json_raw = nullptr;
    sd_bus_message_read(reply, "s", &json_raw);
    json j = json::parse(json_raw ? json_raw : "[]");
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    if (as_json) {
        std::cout << j.dump(2) << "\n";
        return 0;
    }

    if (!j.is_array() || j.empty()) {
        std::cout << "No configured layers reported yet. Is the keyboard connected?\n";
        return 0;
    }

    std::cout << std::format("{:<6}{:<6}{:<16}Active\n", "Idx", "ID", "Name")
              << std::string(34, '-') << "\n";

    for (const auto &item : j) {
        uint32_t idx = item.value("index", 0);
        uint32_t id = item.value("id", 0);
        std::string name = item.value("name", "");
        bool active = item.value("active", false);

        std::cout << std::format("{:<6}{:<6}{:<16}{}\n", idx, id, name, active ? "*" : "");
    }

    return 0;
}

int cmd_keymap(sd_bus *bus, uint32_t layer_idx, bool refresh, bool as_json) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;

    int r = sd_bus_call_method(bus,
                               "org.freedesktop.Strata",
                               "/org/freedesktop/Strata/Device0",
                               "org.freedesktop.Strata.Device1",
                               "GetKeymap",
                               &error,
                               &reply,
                               "ub",
                               layer_idx,
                               refresh ? 1 : 0);
    if (r < 0) {
        std::cerr << "Failed to get keymap: "
                  << (error.message ? error.message : strerror(-r)) << "\n";
        sd_bus_error_free(&error);
        return 1;
    }

    const char *json_raw = nullptr;
    sd_bus_message_read(reply, "s", &json_raw);
    json j = json::parse(json_raw ? json_raw : "{}");
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    if (as_json) {
        std::cout << j.dump(2) << "\n";
        return 0;
    }

    if (!j.contains("summary") || j["summary"].empty()) {
        std::cout << "No keymap data available. Try --refresh if device is connected.\n";
        return 0;
    }

    auto summary = j["summary"];
    std::cout << std::format("Device: {} (Build ID: {})\n"
                             "Layers: {}, Keys per layer: {}\n\n",
                             j.value("device", "Unknown"),
                             j.value("build_id", "Unknown"),
                             summary.value("layer_count", 0),
                             summary.value("keys_per_layer", 0));

    if (j.contains("bindings") && j["bindings"].is_object()) {
        for (auto it = j["bindings"].begin(); it != j["bindings"].end(); ++it) {
            std::cout << std::format("Layer {} Bindings:\n", it.key())
                      << std::format("  {:<6}{:<18}{:<14}Param 2\n", "Pos", "Behavior", "Param 1")
                      << "  " << std::string(48, '-') << "\n";

            if (it.value().is_array()) {
                for (const auto &b : it.value()) {
                    uint32_t pos = b.value("pos", 0);
                    std::string beh = b.value("behavior", "");
                    uint32_t p1 = b.value("param1", 0);
                    uint32_t p2 = b.value("param2", 0);

                    std::cout << std::format("  {:<6}{:<18}0x{:08x}    {}\n", pos, beh, p1, p2);
                }
            }
            std::cout << "\n";
        }
    } else {
        std::cout << "No bindings cached for this layer yet. Run with --refresh to query hardware.\n";
    }

    if (j.contains("sensor_bindings") && j["sensor_bindings"].is_object() &&
        !j["sensor_bindings"].empty()) {
        for (auto it = j["sensor_bindings"].begin(); it != j["sensor_bindings"].end(); ++it) {
            std::cout << std::format("Layer {} Sensor Bindings:\n", it.key())
                      << std::format("  {:<8}{:<18}{:<14}Param 2 (CCW)\n", "Sensor", "Behavior",
                                     "Param 1 (CW)")
                      << "  " << std::string(52, '-') << "\n";

            if (it.value().is_array()) {
                for (const auto &b : it.value()) {
                    uint32_t s_idx = b.value("sensor", 0);
                    std::string beh = b.value("behavior", "");
                    uint32_t p1 = b.value("param1", 0);
                    uint32_t p2 = b.value("param2", 0);

                    std::cout << std::format("  {:<8}{:<18}0x{:08x}    0x{:08x}\n", s_idx, beh, p1,
                                             p2);
                }
            }
            std::cout << "\n";
        }
    }

    return 0;
}

int cmd_cache(sd_bus *bus, bool clear, bool as_json) {
    if (clear) {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = nullptr;

        int r =
            sd_bus_call_method(bus, "org.freedesktop.Strata", "/org/freedesktop/Strata/Device0",
                               "org.freedesktop.Strata.Device1", "ClearCache", &error, &reply, "");
        if (r < 0) {
            std::cerr << "Failed to clear cache: " << (error.message ? error.message : strerror(-r))
                      << "\n";
            sd_bus_error_free(&error);
            return 1;
        }

        int ok = 0;
        sd_bus_message_read(reply, "b", &ok);
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);

        if (as_json) {
            json j = {{"cleared", ok != 0}};
            std::cout << j.dump(2) << "\n";
        } else {
            std::cout << (ok ? "Keymap cache successfully cleared.\n" : "Failed to clear cache.\n");
        }
        return ok ? 0 : 1;
    }

    // By default, show cache status from GetStatus
    return cmd_status(bus, as_json);
}

int signal_callback(sd_bus_message *m, void *userdata, sd_bus_error *) {
    bool as_json = (userdata != nullptr);
    const char *member = sd_bus_message_get_member(m);
    if (!member)
        return 0;

    std::string member_str(member);

    if (member_str == "LayerChanged") {
        uint32_t idx = 0;
        const char *name = nullptr;
        uint32_t mask = 0;
        const char *build = nullptr;
        sd_bus_message_read(m, "usus", &idx, &name, &mask, &build);

        if (as_json) {
            json j = {{"event", "layer_changed"},
                      {"index", idx},
                      {"name", name ? name : ""},
                      {"mask", mask},
                      {"build_id", build ? build : ""}};
            std::cout << j.dump() << std::endl;
        } else {
            std::cout << "[LayerChanged] index=" << idx << " name='" << (name ? name : "") << "'"
                      << " mask=0x" << std::hex << std::setfill('0') << std::setw(8) << mask
                      << std::dec << " build=" << (build ? build : "") << "\n";
        }
    } else if (member_str == "DeviceConnected") {
        const char *name = nullptr;
        const char *node = nullptr;
        const char *build = nullptr;
        sd_bus_message_read(m, "sss", &name, &node, &build);

        if (as_json) {
            json j = {{"event", "device_connected"},
                      {"name", name ? name : ""},
                      {"node", node ? node : ""},
                      {"build_id", build ? build : ""}};
            std::cout << j.dump() << std::endl;
        } else {
            std::cout << "[DeviceConnected] " << (name ? name : "") << " on " << (node ? node : "")
                      << " (build " << (build ? build : "") << ")\n";
        }
    } else if (member_str == "DeviceDisconnected") {
        const char *node = nullptr;
        sd_bus_message_read(m, "s", &node);

        if (as_json) {
            json j = {{"event", "device_disconnected"}, {"node", node ? node : ""}};
            std::cout << j.dump() << std::endl;
        } else {
            std::cout << "[DeviceDisconnected] node=" << (node ? node : "") << "\n";
        }
    } else if (member_str == "KeymapLoaded") {
        const char *build = nullptr;
        const char *src = nullptr;
        uint32_t layers = 0;
        sd_bus_message_read(m, "ssu", &build, &src, &layers);

        if (as_json) {
            json j = {{"event", "keymap_loaded"},
                      {"build_id", build ? build : ""},
                      {"source", src ? src : ""},
                      {"layers", layers}};
            std::cout << j.dump() << std::endl;
        } else {
            std::cout << "[KeymapLoaded] source=" << (src ? src : "")
                      << " build=" << (build ? build : "") << " layers=" << layers << "\n";
        }
    } else if (member_str == "LayerBindingsLoaded") {
        uint32_t layer = 0;
        uint32_t count = 0;
        sd_bus_message_read(m, "uu", &layer, &count);

        if (as_json) {
            json j = {{"event", "layer_bindings_loaded"}, {"layer", layer}, {"count", count}};
            std::cout << j.dump() << std::endl;
        } else {
            std::cout << "[LayerBindingsLoaded] layer=" << layer << " count=" << count << "\n";
        }
    }

    return 0;
}

int cmd_listen(sd_bus *bus, bool as_json) {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    int r = sd_bus_match_signal(bus, nullptr, "org.freedesktop.Strata",
                                "/org/freedesktop/Strata/Device0", "org.freedesktop.Strata.Device1",
                                nullptr, signal_callback,
                                as_json ? reinterpret_cast<void *>(1) : nullptr);
    if (r < 0) {
        std::cerr << "Failed to register signal listener: " << strerror(-r) << "\n";
        return 1;
    }

    if (!as_json) {
        std::cout << "Listening for Strata events on D-Bus (Press Ctrl+C to exit)...\n";
    }

    while (g_running) {
        int pr = sd_bus_process(bus, nullptr);
        if (pr < 0) {
            break;
        }
        if (pr == 0) {
            sd_bus_wait(bus, 500000); // 500ms
        }
    }

    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    if (command == "-h" || command == "--help" || command == "help") {
        print_help(argv[0]);
        return 0;
    }

    bool as_json = false;
    bool refresh = false;
    bool clear = false;
    uint32_t layer_idx = 255;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--json") {
            as_json = true;
        } else if (arg == "--refresh") {
            refresh = true;
        } else if (arg == "--clear") {
            clear = true;
        } else if (arg == "--layer" && i + 1 < argc) {
            layer_idx = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
    }

    sd_bus *bus = connect_bus();
    if (!bus) {
        return 1;
    }

    int ret = 0;
    if (command == "status") {
        ret = cmd_status(bus, as_json);
    } else if (command == "layers") {
        ret = cmd_layers(bus, as_json);
    } else if (command == "keymap") {
        ret = cmd_keymap(bus, layer_idx, refresh, as_json);
    } else if (command == "cache") {
        ret = cmd_cache(bus, clear, as_json);
    } else if (command == "listen") {
        ret = cmd_listen(bus, as_json);
    } else {
        std::cerr << "Unknown command: " << command << "\n\n";
        print_help(argv[0]);
        ret = 1;
    }

    sd_bus_flush_close_unref(bus);
    return ret;
}
