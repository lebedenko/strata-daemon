#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace strata::protocol {

inline constexpr size_t RawReportSize = 32;
inline constexpr size_t MaxLayerNameLen = 16;
inline constexpr size_t BuildIdLen = 8;
inline constexpr size_t MaxBehaviorNameLen = 12;

// Message types from Keyboard -> Host
inline constexpr uint8_t MsgLayerState = 0x01;
inline constexpr uint8_t MsgKeymapSummary = 0x02;
inline constexpr uint8_t MsgLayerInfo = 0x03;
inline constexpr uint8_t MsgLayerBinding = 0x04;
inline constexpr uint8_t MsgSensorBinding = 0x05;

// Command types from Host -> Keyboard
inline constexpr uint8_t CmdGetCurrentLayer = 0x01;
inline constexpr uint8_t CmdGetKeymapSummary = 0x02;
inline constexpr uint8_t CmdGetLayerInfo = 0x03;
inline constexpr uint8_t CmdGetLayerBinding = 0x04;
inline constexpr uint8_t CmdGetSensorBinding = 0x05;

#pragma pack(push, 1)

struct LayerStateReport {
    uint8_t msgType{MsgLayerState};
    uint8_t layerIndex{0};
    uint32_t layerState{0};
    uint8_t nameLen{0};
    char name[MaxLayerNameLen]{0};
    char buildId[BuildIdLen]{0};
    uint8_t reserved[1]{0};
};
static_assert(sizeof(LayerStateReport) == RawReportSize, "LayerStateReport must be 32 bytes");

struct KeymapSummaryReport {
    uint8_t msgType{MsgKeymapSummary};
    uint8_t layerCount{0};
    uint8_t keysPerLayer{0};
    uint8_t defaultLayer{0};
    char buildId[BuildIdLen]{0};
    uint8_t sensorsPerLayer{0};
    uint8_t reserved[19]{0};
};
static_assert(sizeof(KeymapSummaryReport) == RawReportSize, "KeymapSummaryReport must be 32 bytes");

struct LayerInfoReport {
    uint8_t msgType{MsgLayerInfo};
    uint8_t layerIndex{0};
    uint8_t layerId{0};
    uint8_t nameLen{0};
    char name[MaxLayerNameLen]{0};
    uint8_t isActive{0};
    uint8_t reserved[11]{0};
};
static_assert(sizeof(LayerInfoReport) == RawReportSize, "LayerInfoReport must be 32 bytes");

struct LayerBindingReport {
    uint8_t msgType{MsgLayerBinding};
    uint8_t layerIndex{0};
    uint8_t bindingIndex{0};
    char behaviorName[MaxBehaviorNameLen]{0};
    uint32_t param1{0};
    uint32_t param2{0};
    uint8_t reserved[9]{0};
};
static_assert(sizeof(LayerBindingReport) == RawReportSize, "LayerBindingReport must be 32 bytes");

struct SensorBindingReport {
    uint8_t msgType{MsgSensorBinding};
    uint8_t layerIndex{0};
    uint8_t sensorIndex{0};
    char behaviorName[MaxBehaviorNameLen]{0};
    uint32_t param1{0};
    uint32_t param2{0};
    uint8_t reserved[9]{0};
};
static_assert(sizeof(SensorBindingReport) == RawReportSize, "SensorBindingReport must be 32 bytes");

#pragma pack(pop)

inline std::string cleanString(const char *buf, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && buf[len] != '\0') {
        ++len;
    }
    return std::string(buf, len);
}

inline std::array<uint8_t, RawReportSize> makeGetCurrentLayerQuery() {
    std::array<uint8_t, RawReportSize> buf{};
    buf[0] = CmdGetCurrentLayer;
    return buf;
}

inline std::array<uint8_t, RawReportSize> makeGetKeymapSummaryQuery() {
    std::array<uint8_t, RawReportSize> buf{};
    buf[0] = CmdGetKeymapSummary;
    return buf;
}

inline std::array<uint8_t, RawReportSize> makeGetLayerInfoQuery(uint8_t layerIndex) {
    std::array<uint8_t, RawReportSize> buf{};
    buf[0] = CmdGetLayerInfo;
    buf[1] = layerIndex;
    return buf;
}

inline std::array<uint8_t, RawReportSize> makeGetLayerBindingQuery(uint8_t layerIndex,
                                                                   uint8_t bindingIndex) {
    std::array<uint8_t, RawReportSize> buf{};
    buf[0] = CmdGetLayerBinding;
    buf[1] = layerIndex;
    buf[2] = bindingIndex;
    return buf;
}

inline std::array<uint8_t, RawReportSize> makeGetSensorBindingQuery(uint8_t layerIndex,
                                                                    uint8_t sensorIndex) {
    std::array<uint8_t, RawReportSize> buf{};
    buf[0] = CmdGetSensorBinding;
    buf[1] = layerIndex;
    buf[2] = sensorIndex;
    return buf;
}

} // namespace strata::protocol
