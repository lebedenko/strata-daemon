#include "qmk_voyager_device.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <vector>

#include "common/logger.hpp"

namespace strata::device {

QmkVoyagerDevice::QmkVoyagerDevice(std::string deviceNode, std::string deviceName, std::string id,
                                   std::string layoutHash, std::string layoutRev)
    : deviceNode_(std::move(deviceNode))
    , name_(std::move(deviceName))
    , id_(std::move(id))
    , layoutHash_(std::move(layoutHash))
    , layoutRev_(std::move(layoutRev)) {
    if (id_.empty()) {
        auto slash = deviceNode_.find_last_of('/');
        id_ =
            "voyager-" + (slash != std::string::npos ? deviceNode_.substr(slash + 1) : deviceNode_);
    }
}

QmkVoyagerDevice::~QmkVoyagerDevice() {
    close();
}

bool QmkVoyagerDevice::open() {
    if (fd_ >= 0) {
        return true;
    }

    fd_ = ::open(deviceNode_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        log::error("Failed to open Voyager device {}: {}", deviceNode_, strerror(errno));
        return false;
    }

    log::info("Opened Voyager HID device: {} ({})", deviceNode_, name_);

    // 1. Check protocol version
    auto verQuery = protocol::oryx::makeGetProtocolVersionQuery();
    if (!writeReport(verQuery)) {
        log::warn("Failed to write protocol version query to {}", deviceNode_);
    } else {
        std::array<uint8_t, protocol::oryx::ReportSize> resp{};
        if (readFrameWithTimeout(resp, 500)) {
            if (resp[0] == protocol::oryx::evt::GetProtocolVersion) {
                uint8_t protoVer = resp[1];
                log::info("Voyager reported protocol version: 0x{:02x}", protoVer);
            }
        }
    }

    // 2. Pair with device
    auto pairQuery = protocol::oryx::makePairingInitQuery();
    if (!writeReport(pairQuery)) {
        log::warn("Failed to send pairing init to {}", deviceNode_);
    } else {
        std::array<uint8_t, protocol::oryx::ReportSize> resp{};
        for (int i = 0; i < 3; ++i) {
            if (readFrameWithTimeout(resp, 500)) {
                if (resp[0] == protocol::oryx::evt::PairingSuccess) {
                    log::info("Voyager pairing successful");
                    break;
                }
                if (resp[0] == protocol::oryx::evt::Layer) {
                    handleReport(resp);
                }
            }
        }
    }

    // 3. Query firmware version
    auto fwQuery = protocol::oryx::makeGetFwVersionQuery();
    if (writeReport(fwQuery)) {
        std::array<uint8_t, protocol::oryx::ReportSize> resp{};
        if (readFrameWithTimeout(resp, 500) && resp[0] == protocol::oryx::evt::GetFwVersion) {
            std::string ver;
            for (size_t i = 1;
                 i < resp.size() && resp[i] != protocol::oryx::StopByte && resp[i] != 0; ++i) {
                ver.push_back(static_cast<char>(resp[i]));
            }
            if (!ver.empty()) {
                buildId_ = ver;
                log::info("Voyager firmware version: {}", buildId_);
            }
        }
    }

    return true;
}

void QmkVoyagerDevice::close() {
    if (fd_ >= 0) {
        auto disQuery = protocol::oryx::makeDisconnectQuery();
        writeReport(disQuery);

        ::close(fd_);
        fd_ = -1;
        log::info("Closed Voyager HID device: {}", deviceNode_);
    }
}

bool QmkVoyagerDevice::readFrameWithTimeout(std::array<uint8_t, protocol::oryx::ReportSize> &outBuf,
                                            int timeoutMs) {
    if (fd_ < 0) {
        return false;
    }

    struct pollfd pfd = {};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeoutMs);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        ssize_t n = ::read(fd_, outBuf.data(), outBuf.size());
        return n > 0;
    }
    return false;
}

void QmkVoyagerDevice::poll(std::chrono::milliseconds timeout) {
    if (fd_ < 0) {
        return;
    }

    struct pollfd pfd = {};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (ret < 0) {
        if (errno != EINTR) {
            log::warn("Error polling Voyager {}: {}", deviceNode_, strerror(errno));
            close();
            if (onDisconnect_) {
                onDisconnect_();
            }
        }
        return;
    }

    if (ret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        log::info("Voyager device disconnected: {}", deviceNode_);
        close();
        if (onDisconnect_) {
            onDisconnect_();
        }
        return;
    }

    if (ret > 0 && (pfd.revents & POLLIN)) {
        std::array<uint8_t, 32> buf{};
        ssize_t bytesRead = ::read(fd_, buf.data(), buf.size());
        if (bytesRead > 0) {
            handleReport(std::span<const uint8_t>(buf.data(), static_cast<size_t>(bytesRead)));
        } else if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log::warn("Error reading from Voyager {}: {}", deviceNode_, strerror(errno));
            close();
            if (onDisconnect_) {
                onDisconnect_();
            }
        }
    }
}

void QmkVoyagerDevice::handleReport(std::span<const uint8_t> data) {
    if (data.empty()) {
        return;
    }

    uint8_t evt = data[0];
    switch (evt) {
    case protocol::oryx::evt::Layer: {
        if (data.size() > 1) {
            activeLayer_ = data[1];
            uint32_t mask = (1u << activeLayer_);
            std::string name = "Layer " + std::to_string(activeLayer_);
            log::info("Voyager active layer changed: {} (0x{:08x})", activeLayer_, mask);
            if (onLayerState_) {
                onLayerState_(activeLayer_, mask, name, buildId_);
            }
        }
        break;
    }
    case protocol::oryx::evt::KeyDown: {
        if (data.size() >= 3 && onKeyEvent_) {
            onKeyEvent_(data[1], data[2], true);
        }
        break;
    }
    case protocol::oryx::evt::KeyUp: {
        if (data.size() >= 3 && onKeyEvent_) {
            onKeyEvent_(data[1], data[2], false);
        }
        break;
    }
    case protocol::oryx::evt::Error: {
        if (data.size() > 1) {
            log::warn("Voyager reported error code: 0x{:02x}", data[1]);
        }
        break;
    }
    default:
        break;
    }
}

bool QmkVoyagerDevice::writeReport(std::span<const uint8_t> data) {
    if (fd_ < 0) {
        return false;
    }

    // Linux hidraw requires leading 0x00 report ID byte for non-numbered reports
    std::vector<uint8_t> payload;
    if (data.size() == protocol::oryx::ReportSize) {
        payload.reserve(data.size() + 1);
        payload.push_back(0x00);
        payload.insert(payload.end(), data.begin(), data.end());
    } else {
        payload.assign(data.begin(), data.end());
    }

    ssize_t written = ::write(fd_, payload.data(), payload.size());
    if (written < 0) {
        log::warn("Error writing to Voyager {}: {}", deviceNode_, strerror(errno));
        return false;
    }

    return static_cast<size_t>(written) == payload.size();
}

bool QmkVoyagerDevice::setLayer(uint8_t layer, bool lock) {
    auto q = protocol::oryx::makeSetLayerQuery(layer, lock);
    return writeReport(q);
}

bool QmkVoyagerDevice::setRgbControl(bool enable) {
    auto q = protocol::oryx::makeRgbControlQuery(enable);
    return writeReport(q);
}

bool QmkVoyagerDevice::setRgbLed(uint8_t led, uint8_t r, uint8_t g, uint8_t b) {
    auto q = protocol::oryx::makeSetRgbLedQuery(led, r, g, b);
    return writeReport(q);
}

bool QmkVoyagerDevice::setRgbAll(uint8_t r, uint8_t g, uint8_t b) {
    auto q = protocol::oryx::makeSetRgbLedAllQuery(r, g, b);
    return writeReport(q);
}

bool QmkVoyagerDevice::updateBrightness(bool increase) {
    auto q = protocol::oryx::makeUpdateBrightnessQuery(increase);
    return writeReport(q);
}

} // namespace strata::device
