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

struct LayerInfo {
    uint8_t index{0};
    uint8_t id{0};
    std::string name;
    bool isActive{false};
};

struct KeyBinding {
    uint8_t pos{0};
    std::string behavior;
    uint32_t param1{0};
    uint32_t param2{0};
};

struct KeymapSummary {
    uint8_t layerCount{0};
    uint8_t keysPerLayer{0};
    uint8_t defaultLayer{0};
    std::string buildId;
};

struct KeymapData {
    std::string deviceName;
    std::string buildId;
    KeymapSummary summary;
    std::vector<LayerInfo> layers;
    std::unordered_map<uint8_t, std::vector<KeyBinding>> bindings;
};

struct DeviceStatus {
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
