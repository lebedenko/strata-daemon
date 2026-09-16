#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <systemd/sd-daemon.h>

#include "cache/keymap_cache.hpp"
#include "common/logger.hpp"
#include "common/types.hpp"
#include "device/device_manager.hpp"
#include "ipc/dbus_server.hpp"

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP) {
        g_running = false;
    }
}

void print_help(std::string_view prog) {
    std::cout
        << "Usage: " << prog << " [options]\n\n"
        << "Options:\n"
        << "  -v, --verbose, -d, --debug   Enable debug logging (overrides STRATAD_LOG_LEVEL)\n"
        << "  -q, --quiet                  Suppress informational logs\n"
        << "  -h, --help                   Show this help message\n";
}

} // namespace

int main(int argc, char *argv[]) {
    bool verbose = false;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-v" || arg == "--verbose" || arg == "-d" || arg == "--debug") {
            verbose = true;
        } else if (arg == "-q" || arg == "--quiet") {
            quiet = true;
        } else if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_help(argv[0]);
            return 1;
        }
    }

    strata::log::init(verbose);
    if (quiet && !verbose) {
        strata::log::setLevel(strata::log::Level::Warn);
    }

    LOG_INFO("Starting stratad (Strata keyboard daemon)...");

    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    strata::KeymapCache cache;
    strata::DeviceStatus current_status{};
    strata::KeymapData current_keymap{};

    std::unique_ptr<strata::DBusServer> dbus_server;

    strata::device::DeviceManager device_mgr;

    device_mgr.setOnConnect([&](strata::device::IDevice *dev) {
        current_status.connected = true;
        current_status.name = dev->name();
        current_status.node = dev->deviceNode();
        current_status.buildId = dev->buildId();
        current_status.activeLayerIndex = 0;
        current_status.activeLayerName = "unknown";
        current_status.activeLayerMask = 0;
        current_status.layersCount = 0;
        current_status.cached = false;

        LOG_INFO("Device connected: {} ({})", current_status.name, current_status.node);
        if (dbus_server) {
            dbus_server->emit_device_connected(current_status.name, current_status.node,
                                               current_status.buildId);
        }

        dev->setOnLayerState(
            [&](uint8_t index, uint32_t mask, std::string name, std::string buildId) {
                current_status.activeLayerIndex = index;
                current_status.activeLayerMask = mask;
                if (!buildId.empty() && current_status.buildId.empty()) {
                    current_status.buildId = buildId;
                }

                if (name.empty()) {
                    for (const auto &l : current_keymap.layers) {
                        if (l.index == index) {
                            name = l.name;
                            break;
                        }
                    }
                }
                if (name.empty()) {
                    name = std::to_string(index);
                }
                current_status.activeLayerName = name;

                // Notice: Layer updates are logged only at debug level to keep normal logs clean
                LOG_DEBUG("Layer changed: index={}, name={}, mask=0x{:x}", index, name, mask);

                if (dbus_server) {
                    dbus_server->emit_layer_changed(index, name, mask, current_status.buildId);
                }
            });

        dev->setOnSummary([&](const strata::KeymapSummary &summary) {
            LOG_INFO("Received keymap summary: {} layers, {} keys/layer, build={}",
                     summary.layerCount, summary.keysPerLayer, summary.buildId);
            current_status.buildId = summary.buildId;
            current_status.layersCount = summary.layerCount;

            auto cached = cache.load(current_status.name, summary.buildId);
            if (cached && !cached->layers.empty()) {
                current_keymap = std::move(*cached);
                current_status.cached = true;
                LOG_INFO("Loaded keymap for {} (build {}) from cache ({} layers)",
                         current_status.name, summary.buildId, current_keymap.layers.size());
                if (dbus_server) {
                    dbus_server->emit_keymap_loaded(summary.buildId, "cache", summary.layerCount);
                }
            } else {
                current_keymap.deviceName = current_status.name;
                current_keymap.buildId = summary.buildId;
                current_keymap.summary = summary;
                current_keymap.layers.clear();
                current_keymap.bindings.clear();
                current_status.cached = false;

                LOG_INFO("Cache miss for build {}. Discovering keymap from hardware...",
                         summary.buildId);
                if (auto *active = device_mgr.activeDevice()) {
                    active->queryLayerInfo(0);
                }
            }
        });

        dev->setOnLayerInfo([&](const strata::LayerInfo &info) {
            LOG_DEBUG("Received layer info: index={}, name='{}'", info.index, info.name);

            auto it =
                std::find_if(current_keymap.layers.begin(), current_keymap.layers.end(),
                             [&](const strata::LayerInfo &l) { return l.index == info.index; });
            if (it != current_keymap.layers.end()) {
                *it = info;
            } else {
                current_keymap.layers.push_back(info);
            }

            std::sort(current_keymap.layers.begin(), current_keymap.layers.end(),
                      [](const strata::LayerInfo &a, const strata::LayerInfo &b) {
                          return a.index < b.index;
                      });

            if (current_keymap.layers.size() < current_keymap.summary.layerCount) {
                if (auto *active = device_mgr.activeDevice()) {
                    active->queryLayerInfo(static_cast<uint8_t>(current_keymap.layers.size()));
                }
            } else {
                cache.save(current_keymap);
                current_status.cached = true;
                LOG_INFO("Keymap discovery complete ({} layers). Saved to cache.",
                         current_keymap.layers.size());
                if (dbus_server) {
                    dbus_server->emit_keymap_loaded(current_keymap.buildId, "device",
                                                    current_keymap.summary.layerCount);
                }
            }
        });

        dev->setOnBinding([&](uint8_t layer, const strata::KeyBinding &binding) {
            LOG_DEBUG("Received binding: layer={}, pos={}, behavior={}", layer, binding.pos,
                      binding.behavior);

            auto &list = current_keymap.bindings[layer];
            auto it = std::find_if(list.begin(), list.end(), [&](const strata::KeyBinding &b) {
                return b.pos == binding.pos;
            });
            if (it != list.end()) {
                *it = binding;
            } else {
                list.push_back(binding);
            }
            std::sort(list.begin(), list.end(),
                      [](const strata::KeyBinding &a, const strata::KeyBinding &b) {
                          return a.pos < b.pos;
                      });

            if (binding.pos + 1 < current_keymap.summary.keysPerLayer) {
                if (auto *active = device_mgr.activeDevice()) {
                    active->queryLayerBinding(layer, static_cast<uint8_t>(binding.pos + 1));
                }
            } else {
                cache.update_bindings(current_status.name, current_status.buildId, layer, list);
                LOG_INFO("Loaded all {} bindings for layer {}", list.size(), layer);
                if (dbus_server) {
                    dbus_server->emit_layer_bindings_loaded(layer,
                                                            static_cast<uint32_t>(list.size()));
                }
            }
        });

        dev->setOnDisconnect([&]() {
            LOG_INFO("Device disconnected: {}", current_status.node);
            if (dbus_server) {
                dbus_server->emit_device_disconnected(current_status.node);
            }
            current_status = strata::DeviceStatus{};
        });

        dev->queryCurrentLayer();
        dev->queryKeymapSummary();
    });

    device_mgr.setOnDisconnect([&](const std::string &node) {
        LOG_INFO("Device removed: {}", node);
        if (dbus_server) {
            dbus_server->emit_device_disconnected(node);
        }
        current_status = strata::DeviceStatus{};
    });

    // Initialize D-Bus server callbacks
    strata::DBusServer::Callbacks dbus_cbs;
    dbus_cbs.get_status = [&]() { return current_status; };
    dbus_cbs.get_layers = [&]() { return current_keymap.layers; };
    dbus_cbs.get_keymap = [&](uint32_t layer_idx) -> std::optional<strata::KeymapData> {
        if (layer_idx != 255 &&
            !current_keymap.bindings.contains(static_cast<uint8_t>(layer_idx))) {
            if (auto *dev = device_mgr.activeDevice()) {
                if (dev->isOpen() && current_keymap.summary.keysPerLayer > 0) {
                    LOG_INFO("Fetching bindings for layer {} from hardware...", layer_idx);
                    dev->queryLayerBinding(static_cast<uint8_t>(layer_idx), 0);
                }
            }
        }
        if (current_keymap.buildId.empty()) {
            return std::nullopt;
        }
        return current_keymap;
    };
    dbus_cbs.refresh_keymap = [&]() -> bool {
        auto *dev = device_mgr.activeDevice();
        if (!dev || !dev->isOpen()) {
            LOG_WARN("Cannot refresh keymap: device not connected");
            return false;
        }
        LOG_INFO("Refreshing keymap from hardware...");
        current_keymap.layers.clear();
        current_keymap.bindings.clear();
        dev->queryKeymapSummary();
        return true;
    };
    dbus_cbs.clear_cache = [&]() -> bool {
        LOG_INFO("Clearing keymap cache...");
        return cache.clear();
    };

    dbus_server = std::make_unique<strata::DBusServer>(std::move(dbus_cbs));
    if (!dbus_server->init()) {
        LOG_ERROR("Failed to initialize D-Bus server. Exiting.");
        return 1;
    }

    if (!device_mgr.init()) {
        LOG_ERROR("Failed to initialize DeviceManager. Exiting.");
        return 1;
    }

    sd_notify(0, "READY=1");
    LOG_INFO("stratad is ready and running.");

    while (g_running) {
        dbus_server->process();
        device_mgr.poll(std::chrono::milliseconds(20));
    }

    LOG_INFO("stratad stopping...");
    sd_notify(0, "STOPPING=1");

    dbus_server.reset();
    LOG_INFO("stratad stopped cleanly.");
    return 0;
}
