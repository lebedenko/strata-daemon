#pragma once

#include <array>
#include <span>
#include <string>

#include "common/protocol_zmk.hpp"
#include "device/i_device.hpp"

namespace strata::device {

class ZmkRawHidDevice : public IDevice {
public:
    explicit ZmkRawHidDevice(std::string deviceNode, std::string deviceName = "Eyelash Corne");
    ~ZmkRawHidDevice() override;

    DeviceType type() const override { return DeviceType::ZmkRawHid; }
    std::string name() const override { return name_; }
    std::string deviceNode() const override { return deviceNode_; }
    std::string buildId() const override { return buildId_; }
    bool isOpen() const override { return fd_ >= 0; }

    bool open() override;
    void close() override;
    void poll(std::chrono::milliseconds timeout) override;

    void queryCurrentLayer() override;
    void queryKeymapSummary() override;
    void queryLayerInfo(uint8_t layerIndex) override;
    void queryLayerBinding(uint8_t layerIndex, uint8_t bindingIndex) override;
    void querySensorBinding(uint8_t layerIndex, uint8_t sensorIndex) override;

    void setOnLayerState(LayerStateCb cb) override { onLayerState_ = std::move(cb); }
    void setOnSummary(SummaryCb cb) override { onSummary_ = std::move(cb); }
    void setOnLayerInfo(LayerInfoCb cb) override { onLayerInfo_ = std::move(cb); }
    void setOnBinding(BindingCb cb) override { onBinding_ = std::move(cb); }
    void setOnSensorBinding(SensorBindingCb cb) override { onSensorBinding_ = std::move(cb); }
    void setOnDisconnect(DisconnectCb cb) override { onDisconnect_ = std::move(cb); }

private:
    bool writeReport(std::span<const uint8_t> data);
    void handleReport(std::span<const uint8_t> data);

    std::string deviceNode_;
    std::string name_;
    std::string buildId_;
    int fd_{-1};

    LayerStateCb onLayerState_;
    SummaryCb onSummary_;
    LayerInfoCb onLayerInfo_;
    BindingCb onBinding_;
    SensorBindingCb onSensorBinding_;
    DisconnectCb onDisconnect_;
};

} // namespace strata::device
