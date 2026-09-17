#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

    IDevice *activeDevice() const;
    IDevice *getDevice(const std::string &id) const;
    std::vector<IDevice *> allDevices() const;

    bool setActiveDevice(const std::string &id);
    std::string activeDeviceId() const { return activeDeviceId_; }

    static std::string sanitizeId(std::string_view id);

    using ConnectCb = std::function<void(IDevice *device)>;
    using DisconnectCb = std::function<void(const std::string &node, const std::string &id)>;
    using ActiveChangedCb = std::function<void(IDevice *device)>;

    void setOnConnect(ConnectCb cb) { onConnect_ = std::move(cb); }
    void setOnDisconnect(DisconnectCb cb) { onDisconnect_ = std::move(cb); }
    void setOnActiveChanged(ActiveChangedCb cb) { onActiveChanged_ = std::move(cb); }

private:
    void scanExistingDevices();
    void handleUdevEvent();
    std::unique_ptr<IDevice> probeDevice(const std::string &devNode, const std::string &sysPath);

    struct ::udev *udev_{nullptr};
    struct ::udev_monitor *monitor_{nullptr};
    int monitorFd_{-1};

    std::unordered_map<std::string, std::unique_ptr<IDevice>> devices_;
    std::string activeDeviceId_;

    ConnectCb onConnect_;
    DisconnectCb onDisconnect_;
    ActiveChangedCb onActiveChanged_;
};

} // namespace strata::device
