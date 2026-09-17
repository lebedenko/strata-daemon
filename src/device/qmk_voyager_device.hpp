#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>

#include "common/protocol_oryx.hpp"
#include "device/i_device.hpp"

namespace strata::device {

class QmkVoyagerDevice : public IDevice {
public:
    explicit QmkVoyagerDevice(std::string deviceNode, std::string deviceName = "ZSA Voyager",
                              std::string id = "", std::string layoutHash = "",
                              std::string layoutRev = "latest");
    ~QmkVoyagerDevice() override;

    std::string id() const override { return id_; }
    DeviceType type() const override { return DeviceType::QmkVoyager; }
    std::string name() const override { return name_; }
    std::string deviceNode() const override { return deviceNode_; }
    std::string buildId() const override {
        if (!layoutHash_.empty()) {
            return layoutRev_.empty() ? layoutHash_ : (layoutHash_ + "_" + layoutRev_);
        }
        return buildId_;
    }
    std::string layoutHash() const noexcept { return layoutHash_; }
    std::string layoutRev() const noexcept { return layoutRev_; }
    void setLayoutRevision(std::string rev) { layoutRev_ = std::move(rev); }

    DeviceCapability capabilities() const override {
        return DeviceCapability::ActiveLayerNotify | DeviceCapability::ReadableKeymap |
               DeviceCapability::ControllableLayer | DeviceCapability::LightingControl |
               DeviceCapability::PhysicalKeyEvents;
    }
    bool isOpen() const override { return fd_ >= 0; }
    int fd() const override { return fd_; }

    bool open() override;
    void close() override;
    void poll(std::chrono::milliseconds timeout) override;

    // Layer control
    bool setLayer(uint8_t layer, bool lock) override;

    // Lighting control
    bool setRgbControl(bool enable) override;
    bool setRgbLed(uint8_t led, uint8_t r, uint8_t g, uint8_t b) override;
    bool setRgbAll(uint8_t r, uint8_t g, uint8_t b) override;
    bool updateBrightness(bool increase) override;

private:
    bool writeReport(std::span<const uint8_t> data);
    void handleReport(std::span<const uint8_t> data);
    bool readFrameWithTimeout(std::array<uint8_t, protocol::oryx::ReportSize> &outBuf,
                              int timeoutMs);

    std::string deviceNode_;
    std::string name_;
    std::string buildId_;
    std::string id_;
    std::string layoutHash_;
    std::string layoutRev_{"latest"};
    int fd_{-1};
    uint8_t activeLayer_{0};
};

} // namespace strata::device
