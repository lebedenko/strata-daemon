#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "device/i_device.hpp"

struct udev;
struct udev_monitor;

namespace strata::device {

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    bool init();
    void poll(std::chrono::milliseconds timeout);

    IDevice *activeDevice() const { return activeDevice_.get(); }

    using ConnectCb = std::function<void(IDevice *device)>;
    using DisconnectCb = std::function<void(const std::string &node)>;

    void setOnConnect(ConnectCb cb) { onConnect_ = std::move(cb); }
    void setOnDisconnect(DisconnectCb cb) { onDisconnect_ = std::move(cb); }

private:
    void scanExistingDevices();
    void handleUdevEvent();
    std::unique_ptr<IDevice> probeDevice(const std::string &devNode, const std::string &sysPath);

    struct ::udev *udev_{nullptr};
    struct ::udev_monitor *monitor_{nullptr};
    int monitorFd_{-1};

    std::unique_ptr<IDevice> activeDevice_;
    ConnectCb onConnect_;
    DisconnectCb onDisconnect_;
};

} // namespace strata::device
