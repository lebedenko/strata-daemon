#include "device_manager.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <libudev.h>
#include <poll.h>
#include <sstream>
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
constexpr std::string_view TwinDialVid = "feed";
constexpr std::string_view TwinDialPid = "2501";

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
    for (auto &[id, dev] : devices_) {
        dev->close();
    }
    devices_.clear();

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

std::string DeviceManager::sanitizeId(std::string_view id) {
    std::string s;
    s.reserve(id.size());
    for (char c : id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_') {
            s.push_back(c);
        } else {
            s.push_back('_');
        }
    }
    return s.empty() ? "device" : s;
}

IDevice *DeviceManager::activeDevice() const {
    if (activeDeviceId_.empty()) {
        return nullptr;
    }
    auto it = devices_.find(activeDeviceId_);
    return it != devices_.end() ? it->second.get() : nullptr;
}

IDevice *DeviceManager::getDevice(const std::string &id) const {
    if (id.empty()) {
        return activeDevice();
    }
    auto it = devices_.find(id);
    if (it != devices_.end()) {
        return it->second.get();
    }
    std::string san = sanitizeId(id);
    auto it2 = devices_.find(san);
    if (it2 != devices_.end()) {
        return it2->second.get();
    }
    for (const auto &[devId, dev] : devices_) {
        if (sanitizeId(devId) == san) {
            return dev.get();
        }
    }
    return nullptr;
}

std::vector<IDevice *> DeviceManager::allDevices() const {
    std::vector<IDevice *> list;
    list.reserve(devices_.size());
    for (const auto &[id, dev] : devices_) {
        list.push_back(dev.get());
    }
    return list;
}

bool DeviceManager::setActiveDevice(const std::string &id) {
    auto *dev = getDevice(id);
    if (!dev) {
        return false;
    }
    std::string actualId = dev->id();
    if (activeDeviceId_ != actualId) {
        activeDeviceId_ = actualId;
        log::info("Active keyboard switched to: {} ({})", dev->name(), actualId);
        if (onActiveChanged_) {
            onActiveChanged_(dev);
        }
    }
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

    std::string uniq;
    std::istringstream stream(uevent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.starts_with("HID_UNIQ=")) {
            uniq = line.substr(9);
            std::replace(uniq.begin(), uniq.end(), '/', '_');
            break;
        }
    }

    bool isCorne = (ueventLower.find(CorneVid) != std::string::npos &&
                    ueventLower.find(CornePid) != std::string::npos) ||
                   ueventLower.find("eyelash corne") != std::string::npos;

    bool isVoyager = (ueventLower.find(VoyagerVid) != std::string::npos &&
                      ueventLower.find(VoyagerPid) != std::string::npos) ||
                     ueventLower.find("voyager") != std::string::npos;

    bool isTwinDial = (ueventLower.find(TwinDialVid) != std::string::npos &&
                       ueventLower.find(TwinDialPid) != std::string::npos) ||
                      ueventLower.find("twindial") != std::string::npos;

    if (isCorne) {
        if (hasRawHidUsagePage(descPath)) {
            log::info("Detected Eyelash Corne Raw HID node at {}", devNode);
            std::string id = uniq.empty() ? "corne" : sanitizeId("corne_" + uniq);
            return std::make_unique<ZmkRawHidDevice>(devNode, "Eyelash Corne", id);
        }
    } else if (isVoyager) {
        if (hasRawHidUsagePage(descPath)) {
            log::info("Detected ZSA Voyager Raw HID node at {}", devNode);
            std::string id = uniq.empty() ? "voyager" : sanitizeId("voyager_" + uniq);
            std::string layoutHash;
            std::string layoutRev = "latest";
            if (!uniq.empty()) {
                size_t sep = uniq.find_first_of("/_");
                if (sep != std::string::npos) {
                    layoutHash = uniq.substr(0, sep);
                    layoutRev = uniq.substr(sep + 1);
                } else {
                    layoutHash = uniq;
                }
            }
            return std::make_unique<QmkVoyagerDevice>(devNode, "ZSA Voyager", id, layoutHash,
                                                      layoutRev);
        }
    } else if (isTwinDial) {
        if (hasRawHidUsagePage(descPath)) {
            log::info("Detected TwinDial 25 Raw HID node at {}", devNode);
            std::string id = uniq.empty() ? "twindial25" : sanitizeId("twindial25_" + uniq);
            return std::make_unique<ZmkRawHidDevice>(devNode, "TwinDial 25", id);
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

    struct udev_list_entry *devEntries = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry = nullptr;

    udev_list_entry_foreach(entry, devEntries) {
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
                    std::string id = candidate->id();
                    IDevice *ptr = candidate.get();
                    devices_[id] = std::move(candidate);
                    if (activeDeviceId_.empty()) {
                        activeDeviceId_ = id;
                    }
                    if (onConnect_) {
                        onConnect_(ptr);
                    }
                }
            }
        }
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
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

        if (act == "add") {
            auto candidate = probeDevice(node, syspath ? syspath : "");
            if (candidate && candidate->open()) {
                std::string id = candidate->id();
                IDevice *ptr = candidate.get();
                devices_[id] = std::move(candidate);
                if (activeDeviceId_.empty()) {
                    activeDeviceId_ = id;
                }
                if (onConnect_) {
                    onConnect_(ptr);
                }
            }
        } else if (act == "remove") {
            for (auto it = devices_.begin(); it != devices_.end(); ++it) {
                if (it->second->deviceNode() == node) {
                    std::string id = it->first;
                    log::info("Monitored device removed: {} ({})", it->second->name(), node);
                    it->second->close();
                    devices_.erase(it);
                    if (onDisconnect_) {
                        onDisconnect_(node, id);
                    }
                    if (activeDeviceId_ == id) {
                        activeDeviceId_ = devices_.empty() ? "" : devices_.begin()->first;
                        if (onActiveChanged_) {
                            onActiveChanged_(activeDevice());
                        }
                    }
                    break;
                }
            }
        }
    }

    udev_device_unref(dev);
}

void DeviceManager::poll(std::chrono::milliseconds timeout) {
    std::vector<struct pollfd> pfds;
    if (monitorFd_ >= 0) {
        pfds.push_back({monitorFd_, POLLIN, 0});
    }
    for (const auto &[id, dev] : devices_) {
        int devFd = dev->fd();
        if (devFd >= 0) {
            pfds.push_back({devFd, POLLIN, 0});
        }
    }

    int ret = ::poll(pfds.data(), pfds.size(), static_cast<int>(timeout.count()));
    if (ret > 0) {
        if (monitorFd_ >= 0 && (pfds[0].revents & POLLIN)) {
            handleUdevEvent();
        }
    }

    for (auto &[id, dev] : devices_) {
        dev->poll(std::chrono::milliseconds(0));
    }
}

} // namespace strata::device
