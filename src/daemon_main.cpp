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
#include <unordered_map>

#include "cache/keymap_cache.hpp"
#include "common/logger.hpp"
#include "common/types.hpp"
#include "device/device_manager.hpp"
#include "device/oryx_layout.hpp"
#include "device/qmk_voyager_device.hpp"
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

    struct sigaction sa = {};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    strata::KeymapCache cache;
    std::unordered_map<std::string, strata::DeviceStatus> device_statuses;
    std::unordered_map<std::string, strata::KeymapData> device_keymaps;

    std::unique_ptr<strata::DBusServer> dbus_server;
    strata::device::DeviceManager device_mgr;

    auto get_status_for = [&](const std::string &id) -> strata::DeviceStatus {
        std::string targetId = id;
        if (targetId.empty()) {
            if (auto *dev = device_mgr.activeDevice()) {
                targetId = dev->id();
            }
        } else {
            if (auto *dev = device_mgr.getDevice(targetId)) {
                targetId = dev->id();
            }
        }
        auto it = device_statuses.find(targetId);
        if (it != device_statuses.end()) {
            return it->second;
        }
        if (auto *dev = device_mgr.getDevice(targetId)) {
            strata::DeviceStatus st{};
            st.id = dev->id();
            st.type =
                (dev->type() == strata::DeviceType::QmkVoyager) ? "qmk_voyager" : "zmk_raw_hid";
            st.capabilities = strata::capabilitiesToStrings(dev->capabilities());
            st.connected = dev->isOpen();
            st.name = dev->name();
            st.node = dev->deviceNode();
            st.buildId = dev->buildId();
            return st;
        }
        return {};
    };

    device_mgr.setOnConnect([&](strata::device::IDevice *dev) {
        std::string id = dev->id();
        auto &status = device_statuses[id];
        status.id = id;
        status.type =
            (dev->type() == strata::DeviceType::QmkVoyager) ? "qmk_voyager" : "zmk_raw_hid";
        status.capabilities = strata::capabilitiesToStrings(dev->capabilities());
        status.connected = true;
        status.name = dev->name();
        status.node = dev->deviceNode();
        status.buildId = dev->buildId();
        status.activeLayerIndex = 0;
        status.activeLayerName = "unknown";
        status.activeLayerMask = 0;
        status.layersCount = 0;
        status.cached = false;

        LOG_INFO("Device connected: {} ({}, ID: {})", status.name, status.node, id);
        if (dbus_server) {
            dbus_server->register_device(dev);
        }

        dev->setOnLayerState([&, id](uint8_t index, uint32_t mask, std::string name,
                                     std::string buildId) {
            auto &st = device_statuses[id];
            st.activeLayerIndex = index;
            st.activeLayerMask = mask;
            if (!buildId.empty() && st.buildId.empty()) {
                st.buildId = buildId;
            }

            if (name.empty() || name.starts_with("Layer ")) {
                auto it = device_keymaps.find(id);
                if (it != device_keymaps.end()) {
                    for (const auto &l : it->second.layers) {
                        if (l.index == index) {
                            name = l.name;
                            break;
                        }
                    }
                }
            }
            if (name.empty()) {
                name = std::to_string(index);
            }
            st.activeLayerName = name;

            LOG_DEBUG("Layer changed on {}: index={}, name={}, mask=0x{:x}", id, index, name, mask);
            if (dbus_server) {
                dbus_server->emit_layer_changed(id, index, name, mask, st.buildId);
            }
        });

        dev->setOnKeyEvent([&, id](uint8_t col, uint8_t row, bool pressed) {
            if (dbus_server) {
                dbus_server->emit_key_event(id, col, row, pressed);
            }
        });

        dev->setOnSummary([&, id](const strata::KeymapSummary &summary) {
            LOG_INFO("Received keymap summary for {}: {} layers, {} keys/layer, build={}", id,
                     summary.layerCount, summary.keysPerLayer, summary.buildId);
            auto &st = device_statuses[id];
            st.buildId = summary.buildId;
            st.layersCount = summary.layerCount;

            auto cached = cache.load(st.name, summary.buildId);
            if (cached && !cached->layers.empty()) {
                device_keymaps[id] = std::move(*cached);
                st.cached = true;
                LOG_INFO("Loaded keymap for {} (build {}) from cache ({} layers)", st.name,
                         summary.buildId, device_keymaps[id].layers.size());
                if (dbus_server) {
                    dbus_server->emit_keymap_loaded(id, summary.buildId, "cache",
                                                    summary.layerCount);
                }
            } else {
                auto &km = device_keymaps[id];
                km.deviceName = st.name;
                km.buildId = summary.buildId;
                km.summary = summary;
                km.layers.clear();
                km.bindings.clear();
                km.sensorBindings.clear();
                st.cached = false;

                LOG_INFO("Cache miss for {} build {}. Discovering keymap from hardware...", id,
                         summary.buildId);
                if (auto *d = device_mgr.getDevice(id)) {
                    d->queryLayerInfo(0);
                }
            }
        });

        dev->setOnLayerInfo([&, id](const strata::LayerInfo &info) {
            LOG_DEBUG("Received layer info for {}: index={}, name='{}'", id, info.index, info.name);

            auto &km = device_keymaps[id];
            auto it =
                std::find_if(km.layers.begin(), km.layers.end(),
                             [&](const strata::LayerInfo &l) { return l.index == info.index; });
            if (it != km.layers.end()) {
                *it = info;
            } else {
                km.layers.push_back(info);
            }

            std::sort(km.layers.begin(), km.layers.end(),
                      [](const strata::LayerInfo &a, const strata::LayerInfo &b) {
                          return a.index < b.index;
                      });

            if (km.layers.size() < km.summary.layerCount) {
                if (auto *d = device_mgr.getDevice(id)) {
                    d->queryLayerInfo(static_cast<uint8_t>(km.layers.size()));
                }
            } else {
                cache.save(km);
                device_statuses[id].cached = true;
                LOG_INFO("Keymap discovery complete for {} ({} layers). Saved to cache.", id,
                         km.layers.size());
                if (dbus_server) {
                    dbus_server->emit_keymap_loaded(id, km.buildId, "device",
                                                    km.summary.layerCount);
                }
            }
        });

        dev->setOnBinding([&, id](uint8_t layer, const strata::KeyBinding &binding) {
            LOG_DEBUG("Received binding for {}: layer={}, pos={}, behavior={}", id, layer,
                      binding.pos, binding.behavior);

            auto &km = device_keymaps[id];
            auto &list = km.bindings[layer];
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

            if (binding.pos + 1 < km.summary.keysPerLayer) {
                if (auto *d = device_mgr.getDevice(id)) {
                    d->queryLayerBinding(layer, static_cast<uint8_t>(binding.pos + 1));
                }
            } else {
                auto &st = device_statuses[id];
                cache.update_bindings(st.name, st.buildId, layer, list);
                LOG_INFO("Loaded all {} bindings for {} layer {}", list.size(), id, layer);
                if (km.summary.sensorsPerLayer > 0) {
                    if (auto *d = device_mgr.getDevice(id)) {
                        d->querySensorBinding(layer, 0);
                    }
                }
                if (dbus_server) {
                    dbus_server->emit_layer_bindings_loaded(id, layer,
                                                            static_cast<uint32_t>(list.size()));
                }
            }
        });

        dev->setOnSensorBinding([&, id](uint8_t layer, const strata::SensorBinding &binding) {
            LOG_DEBUG("Received sensor binding for {}: layer={}, sensor={}, behavior={}", id, layer,
                      binding.sensorIndex, binding.behavior);

            auto &km = device_keymaps[id];
            auto &list = km.sensorBindings[layer];
            auto it = std::find_if(list.begin(), list.end(), [&](const strata::SensorBinding &b) {
                return b.sensorIndex == binding.sensorIndex;
            });
            if (it != list.end()) {
                *it = binding;
            } else {
                list.push_back(binding);
            }
            std::sort(list.begin(), list.end(),
                      [](const strata::SensorBinding &a, const strata::SensorBinding &b) {
                          return a.sensorIndex < b.sensorIndex;
                      });

            if (binding.sensorIndex + 1 < km.summary.sensorsPerLayer) {
                if (auto *d = device_mgr.getDevice(id)) {
                    d->querySensorBinding(layer, static_cast<uint8_t>(binding.sensorIndex + 1));
                }
            } else {
                auto &st = device_statuses[id];
                cache.update_sensor_bindings(st.name, st.buildId, layer, list);
                LOG_INFO("Loaded all {} sensor bindings for {} layer {}", list.size(), id, layer);
            }
        });

        dev->setOnDisconnect([&, id]() {
            LOG_INFO("Device disconnected: {}", id);
            if (dbus_server) {
                dbus_server->unregister_device(id);
            }
            device_statuses.erase(id);
            device_keymaps.erase(id);
        });

        if (dev->type() == strata::DeviceType::QmkVoyager) {
            auto *vDev = static_cast<strata::device::QmkVoyagerDevice *>(dev);
            std::string buildId = vDev->buildId();
            std::string hash = vDev->layoutHash();
            std::string rev = vDev->layoutRev();

            auto cached = cache.load(status.name, buildId);
            if (cached && !cached->layers.empty()) {
                device_keymaps[id] = std::move(*cached);
                status.cached = true;
                status.layersCount = static_cast<uint8_t>(device_keymaps[id].layers.size());
                if (!device_keymaps[id].layers.empty()) {
                    status.activeLayerName = device_keymaps[id].layers[0].name;
                }
                LOG_INFO("Loaded keymap for {} (build {}) from cache ({} layers)", status.name,
                         buildId, device_keymaps[id].layers.size());
                if (dbus_server) {
                    dbus_server->emit_keymap_loaded(id, buildId, "cache",
                                                    static_cast<uint32_t>(status.layersCount));
                }
            } else if (!hash.empty()) {
                LOG_INFO("Fetching layout for {} from Oryx API (hash={}, rev={})...", status.name,
                         hash, rev);
                auto oryxData = strata::oryx::fetchLayout(hash, rev);
                if (oryxData && !oryxData->layers.empty()) {
                    oryxData->deviceName = status.name;
                    oryxData->buildId = buildId;
                    oryxData->summary.buildId = buildId;
                    cache.save(*oryxData);
                    device_keymaps[id] = std::move(*oryxData);
                    status.cached = true;
                    status.layersCount = device_keymaps[id].layers.size();
                    if (!device_keymaps[id].layers.empty()) {
                        status.activeLayerName = device_keymaps[id].layers[0].name;
                    }
                    LOG_INFO("Fetched and cached keymap for {} from Oryx API ({} layers)",
                             status.name, status.layersCount);
                    if (dbus_server) {
                        dbus_server->emit_keymap_loaded(id, buildId, "oryx",
                                                        static_cast<uint32_t>(status.layersCount));
                    }
                } else {
                    LOG_WARN("Failed to fetch layout for {} from Oryx API", status.name);
                }
            }
        } else if (dev->hasCapability(strata::DeviceCapability::ReadableKeymap)) {
            dev->queryCurrentLayer();
            dev->queryKeymapSummary();
        }
    });

    device_mgr.setOnDisconnect([&](const std::string &node, const std::string &id) {
        LOG_INFO("Device removed from manager: {} ({})", node, id);
        if (dbus_server) {
            dbus_server->unregister_device(id);
        }
        device_statuses.erase(id);
        device_keymaps.erase(id);
    });

    device_mgr.setOnActiveChanged([&](strata::device::IDevice *dev) {
        if (dev && dbus_server) {
            std::string id = dev->id();
            std::string path = strata::DBusServer::device_path(id);
            LOG_INFO("Active keyboard changed to: {} ({})", dev->name(), id);
            dbus_server->emit_active_device_changed(id, path);
        }
    });

    // Initialize D-Bus server callbacks
    strata::DBusServer::Callbacks dbus_cbs;
    dbus_cbs.get_devices = [&]() { return device_mgr.allDevices(); };
    dbus_cbs.get_active_device = [&]() { return device_mgr.activeDevice(); };
    dbus_cbs.set_active_device = [&](const std::string &id) {
        return device_mgr.setActiveDevice(id);
    };

    dbus_cbs.get_device_status = [&](const std::string &id) { return get_status_for(id); };

    dbus_cbs.get_layers = [&](const std::string &id) -> std::vector<strata::LayerInfo> {
        std::string targetId = id;
        if (targetId.empty()) {
            if (auto *dev = device_mgr.activeDevice()) {
                targetId = dev->id();
            }
        } else {
            if (auto *dev = device_mgr.getDevice(targetId)) {
                targetId = dev->id();
            }
        }
        auto it = device_keymaps.find(targetId);
        if (it == device_keymaps.end()) {
            return {};
        }
        auto layers = it->second.layers;
        auto st_it = device_statuses.find(targetId);
        if (st_it != device_statuses.end()) {
            for (auto &l : layers) {
                l.isActive = (l.index == st_it->second.activeLayerIndex);
            }
        }
        return layers;
    };

    dbus_cbs.get_keymap = [&](const std::string &id,
                              uint32_t layer_idx) -> std::optional<strata::KeymapData> {
        std::string targetId = id;
        if (targetId.empty()) {
            if (auto *dev = device_mgr.activeDevice()) {
                targetId = dev->id();
            }
        } else {
            if (auto *dev = device_mgr.getDevice(targetId)) {
                targetId = dev->id();
            }
        }
        auto it = device_keymaps.find(targetId);
        if (it == device_keymaps.end()) {
            return std::nullopt;
        }

        auto &km = it->second;
        if (layer_idx != 255) {
            if (!km.bindings.contains(static_cast<uint8_t>(layer_idx))) {
                if (auto *dev = device_mgr.getDevice(targetId)) {
                    if (dev->isOpen() && km.summary.keysPerLayer > 0) {
                        LOG_INFO("Fetching bindings for {} layer {} from hardware...", targetId,
                                 layer_idx);
                        dev->queryLayerBinding(static_cast<uint8_t>(layer_idx), 0);
                    }
                }
            }
            if (km.summary.sensorsPerLayer > 0 &&
                !km.sensorBindings.contains(static_cast<uint8_t>(layer_idx))) {
                if (auto *dev = device_mgr.getDevice(targetId)) {
                    if (dev->isOpen()) {
                        LOG_INFO("Fetching sensor bindings for {} layer {} from hardware...",
                                 targetId, layer_idx);
                        dev->querySensorBinding(static_cast<uint8_t>(layer_idx), 0);
                    }
                }
            }
        }
        if (km.buildId.empty()) {
            return std::nullopt;
        }
        return km;
    };

    dbus_cbs.refresh_keymap = [&](const std::string &id) -> bool {
        std::string targetId = id;
        if (targetId.empty()) {
            if (auto *dev = device_mgr.activeDevice()) {
                targetId = dev->id();
            }
        } else {
            if (auto *dev = device_mgr.getDevice(targetId)) {
                targetId = dev->id();
            }
        }
        auto *dev = device_mgr.getDevice(targetId);
        if (!dev || !dev->isOpen()) {
            LOG_WARN("Cannot refresh keymap: device '{}' not connected", targetId);
            return false;
        }
        if (dev->type() == strata::DeviceType::QmkVoyager) {
            auto *vDev = static_cast<strata::device::QmkVoyagerDevice *>(dev);
            std::string hash = vDev->layoutHash();
            std::string rev = vDev->layoutRev();
            std::string buildId = vDev->buildId();
            LOG_INFO("Refreshing Voyager layout from Oryx API (hash={}, rev={})...", hash, rev);
            auto oryxData = strata::oryx::fetchLayout(hash, rev);
            if (oryxData && !oryxData->layers.empty()) {
                oryxData->deviceName = dev->name();
                oryxData->buildId = buildId;
                oryxData->summary.buildId = buildId;
                cache.save(*oryxData);
                device_keymaps[targetId] = std::move(*oryxData);
                auto &st = device_statuses[targetId];
                st.layersCount = device_keymaps[targetId].layers.size();
                st.cached = true;
                LOG_INFO("Refreshed keymap for {} from Oryx API ({} layers)", dev->name(),
                         st.layersCount);
                if (dbus_server) {
                    dbus_server->emit_keymap_loaded(targetId, buildId, "oryx",
                                                    static_cast<uint32_t>(st.layersCount));
                }
                return true;
            }
            return false;
        }

        LOG_INFO("Refreshing keymap for {} from hardware...", targetId);
        auto &km = device_keymaps[targetId];
        auto &st = device_statuses[targetId];
        cache.remove(st.name, st.buildId);
        km.layers.clear();
        km.bindings.clear();
        km.sensorBindings.clear();
        st.cached = false;
        dev->queryKeymapSummary();
        return true;
    };

    dbus_cbs.clear_cache = [&]() -> bool {
        LOG_INFO("Clearing keymap cache...");
        return cache.clear();
    };

    dbus_cbs.set_layer = [&](const std::string &id, uint8_t layer, bool lock) -> bool {
        auto *dev = id.empty() ? device_mgr.activeDevice() : device_mgr.getDevice(id);
        return dev ? dev->setLayer(layer, lock) : false;
    };

    dbus_cbs.set_rgb_control = [&](const std::string &id, bool enable) -> bool {
        auto *dev = id.empty() ? device_mgr.activeDevice() : device_mgr.getDevice(id);
        return dev ? dev->setRgbControl(enable) : false;
    };

    dbus_cbs.set_rgb_led = [&](const std::string &id, uint8_t led, uint8_t r, uint8_t g,
                               uint8_t b) -> bool {
        auto *dev = id.empty() ? device_mgr.activeDevice() : device_mgr.getDevice(id);
        return dev ? dev->setRgbLed(led, r, g, b) : false;
    };

    dbus_cbs.set_rgb_all = [&](const std::string &id, uint8_t r, uint8_t g, uint8_t b) -> bool {
        auto *dev = id.empty() ? device_mgr.activeDevice() : device_mgr.getDevice(id);
        return dev ? dev->setRgbAll(r, g, b) : false;
    };

    dbus_cbs.update_brightness = [&](const std::string &id, bool increase) -> bool {
        auto *dev = id.empty() ? device_mgr.activeDevice() : device_mgr.getDevice(id);
        return dev ? dev->updateBrightness(increase) : false;
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
