#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace strata {

enum class DeviceType {
    ZmkRawHid,
    QmkVoyager,
};

enum class DeviceCapability : uint32_t {
    None = 0,
    ActiveLayerNotify = 1 << 0,
    ReadableKeymap = 1 << 1,
    ControllableLayer = 1 << 2,
    LightingControl = 1 << 3,
    PhysicalKeyEvents = 1 << 4,
    BatteryReporting = 1 << 5,
};

inline constexpr DeviceCapability operator|(DeviceCapability a, DeviceCapability b) noexcept {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr DeviceCapability operator&(DeviceCapability a, DeviceCapability b) noexcept {
    return static_cast<DeviceCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr DeviceCapability &operator|=(DeviceCapability &a, DeviceCapability b) noexcept {
    a = a | b;
    return a;
}

inline constexpr DeviceCapability &operator&=(DeviceCapability &a, DeviceCapability b) noexcept {
    a = a & b;
    return a;
}

inline constexpr bool hasCapability(DeviceCapability mask, DeviceCapability cap) noexcept {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(cap)) == static_cast<uint32_t>(cap);
}

inline std::vector<std::string> capabilitiesToStrings(DeviceCapability caps) {
    std::vector<std::string> res;
    if (hasCapability(caps, DeviceCapability::ActiveLayerNotify)) {
        res.emplace_back("active_layer_notify");
    }
    if (hasCapability(caps, DeviceCapability::ReadableKeymap)) {
        res.emplace_back("readable_keymap");
    }
    if (hasCapability(caps, DeviceCapability::ControllableLayer)) {
        res.emplace_back("controllable_layer");
    }
    if (hasCapability(caps, DeviceCapability::LightingControl)) {
        res.emplace_back("lighting_control");
    }
    if (hasCapability(caps, DeviceCapability::PhysicalKeyEvents)) {
        res.emplace_back("physical_key_events");
    }
    if (hasCapability(caps, DeviceCapability::BatteryReporting)) {
        res.emplace_back("battery_reporting");
    }
    return res;
}

struct LayerInfo {
    uint8_t index{0};
    uint8_t id{0};
    std::string name;
    bool isActive{false};
};

struct KeyBinding {
    uint8_t pos{0};
    std::string behavior{};
    uint32_t param1{0};
    uint32_t param2{0};
    std::string primaryLabel{};
    std::string secondaryLabel{};
    std::string category{};
    std::string tooltip{};
};

struct SensorBinding {
    uint8_t sensorIndex{0};
    std::string behavior;
    uint32_t param1{0};
    uint32_t param2{0};
};

struct KeymapSummary {
    uint8_t layerCount{0};
    uint8_t keysPerLayer{0};
    uint8_t defaultLayer{0};
    std::string buildId;
    uint8_t sensorsPerLayer{0};
};

struct KeymapData {
    std::string deviceName;
    std::string buildId;
    KeymapSummary summary;
    std::vector<LayerInfo> layers;
    std::unordered_map<uint8_t, std::vector<KeyBinding>> bindings;
    std::unordered_map<uint8_t, std::vector<SensorBinding>> sensorBindings;
};

struct DeviceStatus {
    std::string id;
    std::string type;
    std::vector<std::string> capabilities;
    bool connected{false};
    std::string name;
    std::string node;
    std::string buildId;
    uint8_t activeLayerIndex{0};
    std::string activeLayerName{"unknown"};
    uint32_t activeLayerMask{0};
    size_t layersCount{0};
    bool cached{false};
};

} // namespace strata
