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

    virtual std::string id() const = 0;
    virtual DeviceType type() const = 0;
    virtual std::string name() const = 0;
    virtual std::string deviceNode() const = 0;
    virtual std::string buildId() const = 0;
    virtual DeviceCapability capabilities() const = 0;
    virtual bool isOpen() const = 0;
    virtual int fd() const { return -1; }

    [[nodiscard]] bool hasCapability(DeviceCapability cap) const noexcept {
        return strata::hasCapability(capabilities(), cap);
    }

    virtual bool open() = 0;
    virtual void close() = 0;
    virtual void poll(std::chrono::milliseconds timeout) = 0;

    // Asynchronous query triggers (Keymap)
    virtual void queryCurrentLayer() {}
    virtual void queryKeymapSummary() {}
    virtual void queryLayerInfo([[maybe_unused]] uint8_t layerIndex) {}
    virtual void queryLayerBinding([[maybe_unused]] uint8_t layerIndex,
                                   [[maybe_unused]] uint8_t bindingIndex) {}
    virtual void querySensorBinding([[maybe_unused]] uint8_t layerIndex,
                                    [[maybe_unused]] uint8_t sensorIndex) {}

    // Layer control (Smart layers)
    virtual bool setLayer([[maybe_unused]] uint8_t layer, [[maybe_unused]] bool lock) {
        return false;
    }

    // Lighting control
    virtual bool setRgbControl([[maybe_unused]] bool enable) { return false; }
    virtual bool setRgbLed([[maybe_unused]] uint8_t led, [[maybe_unused]] uint8_t r,
                           [[maybe_unused]] uint8_t g, [[maybe_unused]] uint8_t b) {
        return false;
    }
    virtual bool setRgbAll([[maybe_unused]] uint8_t r, [[maybe_unused]] uint8_t g,
                           [[maybe_unused]] uint8_t b) {
        return false;
    }
    virtual bool updateBrightness([[maybe_unused]] bool increase) { return false; }

    // Callbacks
    using LayerStateCb =
        std::function<void(uint8_t index, uint32_t mask, std::string name, std::string buildId)>;
    using SummaryCb = std::function<void(const KeymapSummary &)>;
    using LayerInfoCb = std::function<void(const LayerInfo &)>;
    using BindingCb = std::function<void(uint8_t layer, const KeyBinding &)>;
    using SensorBindingCb = std::function<void(uint8_t layer, const SensorBinding &)>;
    using KeyEventCb = std::function<void(uint8_t col, uint8_t row, bool pressed)>;
    using DisconnectCb = std::function<void()>;

    virtual void setOnLayerState(LayerStateCb cb) { onLayerState_ = std::move(cb); }
    virtual void setOnSummary(SummaryCb cb) { onSummary_ = std::move(cb); }
    virtual void setOnLayerInfo(LayerInfoCb cb) { onLayerInfo_ = std::move(cb); }
    virtual void setOnBinding(BindingCb cb) { onBinding_ = std::move(cb); }
    virtual void setOnSensorBinding(SensorBindingCb cb) { onSensorBinding_ = std::move(cb); }
    virtual void setOnKeyEvent(KeyEventCb cb) { onKeyEvent_ = std::move(cb); }
    virtual void setOnDisconnect(DisconnectCb cb) { onDisconnect_ = std::move(cb); }

protected:
    LayerStateCb onLayerState_;
    SummaryCb onSummary_;
    LayerInfoCb onLayerInfo_;
    BindingCb onBinding_;
    SensorBindingCb onSensorBinding_;
    KeyEventCb onKeyEvent_;
    DisconnectCb onDisconnect_;
};

} // namespace strata::device
