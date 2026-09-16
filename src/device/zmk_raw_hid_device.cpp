#include "zmk_raw_hid_device.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <vector>

#include "common/logger.hpp"

namespace strata::device {

ZmkRawHidDevice::ZmkRawHidDevice(std::string deviceNode, std::string deviceName)
    : deviceNode_(std::move(deviceNode))
    , name_(std::move(deviceName)) {}

ZmkRawHidDevice::~ZmkRawHidDevice() {
    close();
}

bool ZmkRawHidDevice::open() {
    if (fd_ >= 0) {
        return true;
    }

    fd_ = ::open(deviceNode_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        log::error("Failed to open HID device {}: {}", deviceNode_, strerror(errno));
        return false;
    }

    log::info("Opened HID device: {} ({})", deviceNode_, name_);
    return true;
}

void ZmkRawHidDevice::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        log::info("Closed HID device: {}", deviceNode_);
    }
}

void ZmkRawHidDevice::poll(std::chrono::milliseconds timeout) {
    if (fd_ < 0) {
        return;
    }

    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (ret < 0) {
        if (errno != EINTR) {
            log::warn("Error polling {}: {}", deviceNode_, strerror(errno));
            close();
            if (onDisconnect_) {
                onDisconnect_();
            }
        }
        return;
    }

    if (ret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        log::info("Device disconnected: {}", deviceNode_);
        close();
        if (onDisconnect_) {
            onDisconnect_();
        }
        return;
    }

    if (ret > 0 && (pfd.revents & POLLIN)) {
        std::array<uint8_t, 64> buf{};
        ssize_t bytesRead = ::read(fd_, buf.data(), buf.size());
        if (bytesRead > 0) {
            handleReport(std::span<const uint8_t>(buf.data(), static_cast<size_t>(bytesRead)));
        } else if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log::warn("Error reading from {}: {}", deviceNode_, strerror(errno));
            close();
            if (onDisconnect_) {
                onDisconnect_();
            }
        }
    }
}

bool ZmkRawHidDevice::writeReport(std::span<const uint8_t> data) {
    if (fd_ < 0) {
        return false;
    }

    // Linux hidraw requires leading 0x00 report ID byte for non-numbered reports
    std::vector<uint8_t> payload;
    if (data.size() == protocol::RawReportSize) {
        payload.reserve(data.size() + 1);
        payload.push_back(0x00);
        payload.insert(payload.end(), data.begin(), data.end());
    } else {
        payload.assign(data.begin(), data.end());
    }

    ssize_t written = ::write(fd_, payload.data(), payload.size());
    if (written < 0) {
        log::warn("Error writing to {}: {}", deviceNode_, strerror(errno));
        return false;
    }

    return static_cast<size_t>(written) == payload.size();
}

void ZmkRawHidDevice::handleReport(std::span<const uint8_t> data) {
    if (data.size() < protocol::RawReportSize) {
        return;
    }

    uint8_t msgType = data[0];

    switch (msgType) {
    case protocol::MsgLayerState: {
        const auto *report = reinterpret_cast<const protocol::LayerStateReport *>(data.data());
        std::string name = protocol::cleanString(report->name, protocol::MaxLayerNameLen);
        if (name.empty()) {
            name = std::format("LAYER_{}", report->layerIndex);
        }
        buildId_ = protocol::cleanString(report->buildId, protocol::BuildIdLen);

        log::debug("Layer update: idx={}, name='{}', mask=0x{:08x}, build='{}'", report->layerIndex,
                   name, report->layerState, buildId_);

        if (onLayerState_) {
            onLayerState_(report->layerIndex, report->layerState, name, buildId_);
        }
        break;
    }

    case protocol::MsgKeymapSummary: {
        const auto *report = reinterpret_cast<const protocol::KeymapSummaryReport *>(data.data());
        buildId_ = protocol::cleanString(report->buildId, protocol::BuildIdLen);

        KeymapSummary summary{
            .layerCount = report->layerCount,
            .keysPerLayer = report->keysPerLayer,
            .defaultLayer = report->defaultLayer,
            .buildId = buildId_,
            .sensorsPerLayer = report->sensorsPerLayer,
        };

        log::info(
            "Keymap summary: {} layers, {} keys/layer, {} sensors/layer, default={}, build='{}'",
            summary.layerCount, summary.keysPerLayer, summary.sensorsPerLayer, summary.defaultLayer,
            summary.buildId);

        if (onSummary_) {
            onSummary_(summary);
        }
        break;
    }

    case protocol::MsgLayerInfo: {
        const auto *report = reinterpret_cast<const protocol::LayerInfoReport *>(data.data());
        std::string name = protocol::cleanString(report->name, protocol::MaxLayerNameLen);
        if (name.empty()) {
            name = std::format("LAYER_{}", report->layerIndex);
        }

        LayerInfo info{
            .index = report->layerIndex,
            .id = report->layerId,
            .name = name,
            .isActive = (report->isActive != 0),
        };

        log::debug("Layer info: idx={}, id={}, name='{}', active={}", info.index, info.id,
                   info.name, info.isActive);

        if (onLayerInfo_) {
            onLayerInfo_(info);
        }
        break;
    }

    case protocol::MsgLayerBinding: {
        const auto *report = reinterpret_cast<const protocol::LayerBindingReport *>(data.data());
        std::string behavior =
            protocol::cleanString(report->behaviorName, protocol::MaxBehaviorNameLen);

        KeyBinding binding{
            .pos = report->bindingIndex,
            .behavior = behavior,
            .param1 = report->param1,
            .param2 = report->param2,
        };

        log::debug("Layer binding: layer={}, pos={}, behavior='{}', p1=0x{:x}, p2=0x{:x}",
                   report->layerIndex, binding.pos, binding.behavior, binding.param1,
                   binding.param2);

        if (onBinding_) {
            onBinding_(report->layerIndex, binding);
        }
        break;
    }

    case protocol::MsgSensorBinding: {
        const auto *report = reinterpret_cast<const protocol::SensorBindingReport *>(data.data());
        std::string behavior =
            protocol::cleanString(report->behaviorName, protocol::MaxBehaviorNameLen);

        SensorBinding binding{
            .sensorIndex = report->sensorIndex,
            .behavior = behavior,
            .param1 = report->param1,
            .param2 = report->param2,
        };

        log::debug("Sensor binding: layer={}, sensor={}, behavior='{}', p1=0x{:x}, p2=0x{:x}",
                   report->layerIndex, binding.sensorIndex, binding.behavior, binding.param1,
                   binding.param2);

        if (onSensorBinding_) {
            onSensorBinding_(report->layerIndex, binding);
        }
        break;
    }

    default:
        log::debug("Unknown report msg_type=0x{:02x} (len={})", msgType, data.size());
        break;
    }
}

void ZmkRawHidDevice::queryCurrentLayer() {
    auto query = protocol::makeGetCurrentLayerQuery();
    writeReport(query);
}

void ZmkRawHidDevice::queryKeymapSummary() {
    auto query = protocol::makeGetKeymapSummaryQuery();
    writeReport(query);
}

void ZmkRawHidDevice::queryLayerInfo(uint8_t layerIndex) {
    auto query = protocol::makeGetLayerInfoQuery(layerIndex);
    writeReport(query);
}

void ZmkRawHidDevice::queryLayerBinding(uint8_t layerIndex, uint8_t bindingIndex) {
    auto query = protocol::makeGetLayerBindingQuery(layerIndex, bindingIndex);
    writeReport(query);
}

void ZmkRawHidDevice::querySensorBinding(uint8_t layerIndex, uint8_t sensorIndex) {
    auto query = protocol::makeGetSensorBindingQuery(layerIndex, sensorIndex);
    writeReport(query);
}

} // namespace strata::device
