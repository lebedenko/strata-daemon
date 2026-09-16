#pragma once

#include "device/i_device.hpp"

namespace strata::device {

/**
 * @brief Future driver stub for ZSA Voyager running QMK Raw HID.
 */
class QmkVoyagerDevice : public IDevice {
public:
    explicit QmkVoyagerDevice(std::string deviceNode)
        : deviceNode_(std::move(deviceNode))
        , name_("ZSA Voyager") {}

    DeviceType type() const override { return DeviceType::QmkVoyager; }
    std::string name() const override { return name_; }
    std::string deviceNode() const override { return deviceNode_; }
    std::string buildId() const override { return buildId_; }
    bool isOpen() const override { return fd_ >= 0; }

    bool open() override { return false; }
    void close() override {}
    void poll(std::chrono::milliseconds /*timeout*/) override {}

    void queryCurrentLayer() override {}
    void queryKeymapSummary() override {}
    void queryLayerInfo(uint8_t /*layerIndex*/) override {}
    void queryLayerBinding(uint8_t /*layerIndex*/, uint8_t /*bindingIndex*/) override {}

    void setOnLayerState(LayerStateCb cb) override { onLayerState_ = std::move(cb); }
    void setOnSummary(SummaryCb cb) override { onSummary_ = std::move(cb); }
    void setOnLayerInfo(LayerInfoCb cb) override { onLayerInfo_ = std::move(cb); }
    void setOnBinding(BindingCb cb) override { onBinding_ = std::move(cb); }
    void setOnDisconnect(DisconnectCb cb) override { onDisconnect_ = std::move(cb); }

private:
    std::string deviceNode_;
    std::string name_;
    std::string buildId_;
    int fd_{-1};

    LayerStateCb onLayerState_;
    SummaryCb onSummary_;
    LayerInfoCb onLayerInfo_;
    BindingCb onBinding_;
    DisconnectCb onDisconnect_;
};

} // namespace strata::device
