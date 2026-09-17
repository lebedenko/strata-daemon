#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace strata::protocol::oryx {

inline constexpr uint16_t ZsaVid = 0x3297;
inline constexpr uint16_t VoyagerPid = 0x1977;
inline constexpr uint16_t RawUsagePage = 0xFF60;
inline constexpr uint16_t RawUsage = 0x61;
inline constexpr size_t ReportSize = 32;
inline constexpr uint8_t StopByte = 0xFE;
inline constexpr uint8_t ExpectedProtocolVersion = 0x04;

namespace cmd {
inline constexpr uint8_t GetFwVersion = 0x00;
inline constexpr uint8_t PairingInit = 0x01;
inline constexpr uint8_t Disconnect = 0x03;
inline constexpr uint8_t SetLayer = 0x04;
inline constexpr uint8_t RgbControl = 0x05;
inline constexpr uint8_t SetRgbLed = 0x06;
inline constexpr uint8_t SetStatusLed = 0x07;
inline constexpr uint8_t UpdateBrightness = 0x08;
inline constexpr uint8_t SetRgbLedAll = 0x09;
inline constexpr uint8_t StatusLedControl = 0x0A;
inline constexpr uint8_t GetProtocolVersion = 0xFE;
} // namespace cmd

namespace evt {
inline constexpr uint8_t GetFwVersion = 0x00;
inline constexpr uint8_t PairingSuccess = 0x04;
inline constexpr uint8_t Layer = 0x05;
inline constexpr uint8_t KeyDown = 0x06;
inline constexpr uint8_t KeyUp = 0x07;
inline constexpr uint8_t GetProtocolVersion = 0xFE;
inline constexpr uint8_t Error = 0xFF;
} // namespace evt

using ReportBuffer = std::array<uint8_t, ReportSize>;

inline ReportBuffer makeGetProtocolVersionQuery() {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::GetProtocolVersion;
    return buf;
}

inline ReportBuffer makePairingInitQuery() {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::PairingInit;
    return buf;
}

inline ReportBuffer makeGetFwVersionQuery() {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::GetFwVersion;
    return buf;
}

inline ReportBuffer makeSetLayerQuery(uint8_t layer, bool lock) {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::SetLayer;
    buf[1] = lock ? 0x01 : 0x00;
    buf[2] = layer;
    return buf;
}

inline ReportBuffer makeRgbControlQuery(bool enable) {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::RgbControl;
    buf[1] = enable ? 0x01 : 0x00;
    return buf;
}

inline ReportBuffer makeSetRgbLedQuery(uint8_t led, uint8_t r, uint8_t g, uint8_t b) {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::SetRgbLed;
    buf[1] = led;
    buf[2] = r;
    buf[3] = g;
    buf[4] = b;
    return buf;
}

inline ReportBuffer makeSetRgbLedAllQuery(uint8_t r, uint8_t g, uint8_t b) {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::SetRgbLedAll;
    buf[1] = r;
    buf[2] = g;
    buf[3] = b;
    return buf;
}

inline ReportBuffer makeUpdateBrightnessQuery(bool increase) {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::UpdateBrightness;
    buf[1] = increase ? 0x01 : 0x00;
    return buf;
}

inline ReportBuffer makeDisconnectQuery() {
    ReportBuffer buf;
    buf.fill(StopByte);
    buf[0] = cmd::Disconnect;
    return buf;
}

} // namespace strata::protocol::oryx
