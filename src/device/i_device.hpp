#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "common/types.hpp"

namespace strata::device {

class IDevice {
public:
    virtual ~IDevice() = default;

    virtual DeviceType type() const = 0;
    virtual std::string name() const = 0;
    virtual std::string deviceNode() const = 0;
    virtual std::string buildId() const = 0;
    virtual bool isOpen() const = 0;

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual void poll(std::chrono::milliseconds timeout) = 0;

    // Asynchronous query triggers
    virtual void queryCurrentLayer() = 0;
    virtual void queryKeymapSummary() = 0;
    virtual void queryLayerInfo(uint8_t layerIndex) = 0;
    virtual void queryLayerBinding(uint8_t layerIndex, uint8_t bindingIndex) = 0;

    // Callbacks
    using LayerStateCb =
        std::function<void(uint8_t index, uint32_t mask, std::string name, std::string buildId)>;
    using SummaryCb = std::function<void(const KeymapSummary &)>;
    using LayerInfoCb = std::function<void(const LayerInfo &)>;
    using BindingCb = std::function<void(uint8_t layer, const KeyBinding &)>;
    using DisconnectCb = std::function<void()>;

    virtual void setOnLayerState(LayerStateCb cb) = 0;
    virtual void setOnSummary(SummaryCb cb) = 0;
    virtual void setOnLayerInfo(LayerInfoCb cb) = 0;
    virtual void setOnBinding(BindingCb cb) = 0;
    virtual void setOnDisconnect(DisconnectCb cb) = 0;
};

} // namespace strata::device
