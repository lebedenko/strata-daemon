#pragma once

#include <array>
#include <span>
#include <string>

#include "common/protocol_zmk.hpp"
#include "device/i_device.hpp"

namespace strata::device {

class ZmkRawHidDevice : public IDevice {
public:
    explicit ZmkRawHidDevice(std::string deviceNode, std::string deviceName = "Eyelash Corne",
                             std::string id = "");
    ~ZmkRawHidDevice() override;

    std::string id() const override { return id_; }
    DeviceType type() const override { return DeviceType::ZmkRawHid; }
    std::string name() const override { return name_; }
    std::string deviceNode() const override { return deviceNode_; }
    std::string buildId() const override { return buildId_; }
    DeviceCapability capabilities() const override {
        return DeviceCapability::ActiveLayerNotify | DeviceCapability::ReadableKeymap |
               DeviceCapability::ControllableLayer;
    }
    bool isOpen() const override { return fd_ >= 0; }
    int fd() const override { return fd_; }

    bool open() override;
    void close() override;
    void poll(std::chrono::milliseconds timeout) override;

    void queryCurrentLayer() override;
    void queryKeymapSummary() override;
    void queryLayerInfo(uint8_t layerIndex) override;
    void queryLayerBinding(uint8_t layerIndex, uint8_t bindingIndex) override;
    void querySensorBinding(uint8_t layerIndex, uint8_t sensorIndex) override;

    bool setLayer(uint8_t layer, bool lock) override;

private:
    bool writeReport(std::span<const uint8_t> data);
    void handleReport(std::span<const uint8_t> data);

    std::string deviceNode_;
    std::string name_;
    std::string buildId_;
    std::string id_;
    int fd_{-1};
};

} // namespace strata::device
