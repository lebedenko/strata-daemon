#include "device_manager.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <libudev.h>
#include <poll.h>
#include <unistd.h>

#include "common/logger.hpp"
#include "qmk_voyager_device.hpp"
#include "zmk_raw_hid_device.hpp"

namespace strata::device {

namespace {

constexpr std::string_view CorneVid = "1d50";
constexpr std::string_view CornePid = "615e";
constexpr std::string_view VoyagerVid = "3297";
constexpr std::string_view VoyagerPid = "1977";

// Usage Page (0xFF60) in raw report descriptor
constexpr std::array<uint8_t, 3> RawHidUsagePage = {0x06, 0x60, 0xff};

bool hasRawHidUsagePage(const std::filesystem::path &descPath) {
    if (!std::filesystem::exists(descPath)) {
        return false;
    }

    std::ifstream file(descPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(file), {});
    if (buffer.empty()) {
        return false;
    }

    auto it =
        std::search(buffer.begin(), buffer.end(), RawHidUsagePage.begin(), RawHidUsagePage.end());
    return it != buffer.end();
}

std::string toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

} // namespace

DeviceManager::DeviceManager() = default;

DeviceManager::~DeviceManager() {
    if (activeDevice_) {
        activeDevice_->close();
        activeDevice_.reset();
    }
    if (monitor_) {
        udev_monitor_unref(monitor_);
        monitor_ = nullptr;
    }
    if (udev_) {
        udev_unref(udev_);
        udev_ = nullptr;
    }
}

bool DeviceManager::init() {
    udev_ = udev_new();
    if (!udev_) {
        log::error("Failed to initialize libudev context");
        return false;
    }

    monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
    if (!monitor_) {
        log::error("Failed to create udev monitor");
        return false;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(monitor_, "hidraw", nullptr) < 0) {
        log::error("Failed to add hidraw filter to udev monitor");
        return false;
    }

    if (udev_monitor_enable_receiving(monitor_) < 0) {
        log::error("Failed to enable udev monitor receiving");
        return false;
    }

    monitorFd_ = udev_monitor_get_fd(monitor_);
    log::info("Udev monitor initialized for hidraw devices");

    scanExistingDevices();
    return true;
}

std::unique_ptr<IDevice> DeviceManager::probeDevice(const std::string &devNode,
                                                    const std::string &sysPath) {
    std::filesystem::path p(sysPath);
    std::filesystem::path ueventPath = p / "device" / "uevent";
    std::filesystem::path descPath = p / "device" / "report_descriptor";

    std::string uevent;
    if (std::filesystem::exists(ueventPath)) {
        std::ifstream uf(ueventPath);
        uevent.assign(std::istreambuf_iterator<char>(uf), {});
    }
    std::string ueventLower = toLower(uevent);

    bool isCorne = (ueventLower.find(CorneVid) != std::string::npos &&
                    ueventLower.find(CornePid) != std::string::npos) ||
                   ueventLower.find("eyelash corne") != std::string::npos;

    bool isVoyager = (ueventLower.find(VoyagerVid) != std::string::npos &&
                      ueventLower.find(VoyagerPid) != std::string::npos) ||
                     ueventLower.find("voyager") != std::string::npos;

    if (isCorne) {
        // Confirm raw HID usage page
        if (hasRawHidUsagePage(descPath)) {
            log::info("Detected Eyelash Corne Raw HID node at {}", devNode);
            return std::make_unique<ZmkRawHidDevice>(devNode, "Eyelash Corne");
        }
    } else if (isVoyager) {
        if (hasRawHidUsagePage(descPath)) {
            log::info("Detected ZSA Voyager Raw HID node at {}", devNode);
            return std::make_unique<QmkVoyagerDevice>(devNode);
        }
    }

    return nullptr;
}

void DeviceManager::scanExistingDevices() {
    struct udev_enumerate *enumerate = udev_enumerate_new(udev_);
    if (!enumerate) {
        return;
    }

    udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry = nullptr;

    udev_list_entry_foreach(entry, devices) {
        const char *path = udev_list_entry_get_name(entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev_, path);
        if (!dev) {
            continue;
        }

        const char *devnode = udev_device_get_devnode(dev);
        if (devnode) {
            auto candidate = probeDevice(devnode, path);
            if (candidate) {
                if (candidate->open()) {
                    activeDevice_ = std::move(candidate);
                    udev_device_unref(dev);
                    break;
                }
            }
        }
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);

    if (activeDevice_ && onConnect_) {
        onConnect_(activeDevice_.get());
    }
}

void DeviceManager::handleUdevEvent() {
    struct udev_device *dev = udev_monitor_receive_device(monitor_);
    if (!dev) {
        return;
    }

    const char *action = udev_device_get_action(dev);
    const char *devnode = udev_device_get_devnode(dev);
    const char *syspath = udev_device_get_syspath(dev);

    if (action && devnode) {
        std::string act(action);
        std::string node(devnode);

        if (act == "add" && !activeDevice_) {
            auto candidate = probeDevice(node, syspath ? syspath : "");
            if (candidate && candidate->open()) {
                activeDevice_ = std::move(candidate);
                if (onConnect_) {
                    onConnect_(activeDevice_.get());
                }
            }
        } else if (act == "remove") {
            if (activeDevice_ && activeDevice_->deviceNode() == node) {
                log::info("Monitored device removed: {}", node);
                activeDevice_->close();
                activeDevice_.reset();
                if (onDisconnect_) {
                    onDisconnect_(node);
                }
            }
        }
    }

    udev_device_unref(dev);
}

void DeviceManager::poll(std::chrono::milliseconds timeout) {
    if (monitorFd_ >= 0) {
        struct pollfd pfd{};
        pfd.fd = monitorFd_;
        pfd.events = POLLIN;

        int waitMs = activeDevice_ ? 0 : static_cast<int>(timeout.count());
        int ret = ::poll(&pfd, 1, waitMs);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            handleUdevEvent();
        }
    }

    if (activeDevice_) {
        activeDevice_->poll(timeout);
    }
}

} // namespace strata::device
