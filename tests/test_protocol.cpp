#include <cstdlib>
#include <cstring>
#include <iostream>

#include "common/protocol_oryx.hpp"
#include "common/protocol_zmk.hpp"
#include "common/types.hpp"

#define TEST_ASSERT(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "ASSERTION FAILED: " #cond << " at " << __FILE__ << ":" << __LINE__       \
                      << "\n";                                                                     \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)

using namespace strata::protocol;

void test_report_sizes() {
    static_assert(sizeof(LayerStateReport) == RawReportSize, "LayerStateReport must be 32 bytes");
    static_assert(sizeof(KeymapSummaryReport) == RawReportSize,
                  "KeymapSummaryReport must be 32 bytes");
    static_assert(sizeof(LayerInfoReport) == RawReportSize, "LayerInfoReport must be 32 bytes");
    static_assert(sizeof(LayerBindingReport) == RawReportSize,
                  "LayerBindingReport must be 32 bytes");
    std::cout << "[PASS] Struct sizes verified (32 bytes each)\n";
}

void test_query_generation() {
    auto qLayer = makeGetCurrentLayerQuery();
    TEST_ASSERT(qLayer.size() == RawReportSize);
    TEST_ASSERT(qLayer[0] == CmdGetCurrentLayer);

    auto qSummary = makeGetKeymapSummaryQuery();
    TEST_ASSERT(qSummary.size() == RawReportSize);
    TEST_ASSERT(qSummary[0] == CmdGetKeymapSummary);

    auto qInfo = makeGetLayerInfoQuery(3);
    TEST_ASSERT(qInfo.size() == RawReportSize);
    TEST_ASSERT(qInfo[0] == CmdGetLayerInfo);
    TEST_ASSERT(qInfo[1] == 3);

    auto qBinding = makeGetLayerBindingQuery(2, 14, 42);
    TEST_ASSERT(qBinding.size() == RawReportSize);
    TEST_ASSERT(qBinding[0] == CmdGetLayerBinding);
    TEST_ASSERT(qBinding[1] == 2);
    TEST_ASSERT(qBinding[2] == 14);
    TEST_ASSERT(qBinding[3] == 42); // seq

    auto qSetLayer = makeSetLayerCommand(3, true, 3000, 7);
    TEST_ASSERT(qSetLayer.size() == RawReportSize);
    TEST_ASSERT(qSetLayer[0] == CmdSetLayer);
    TEST_ASSERT(qSetLayer[1] == 3);
    TEST_ASSERT(qSetLayer[2] == 1);
    TEST_ASSERT(qSetLayer[3] == 7); // seq
    uint16_t lease = static_cast<uint16_t>(qSetLayer[4] | (qSetLayer[5] << 8));
    TEST_ASSERT(lease == 3000);

    auto qClearLayer = makeClearLayerCommand(8);
    TEST_ASSERT(qClearLayer.size() == RawReportSize);
    TEST_ASSERT(qClearLayer[0] == CmdClearLayer);
    TEST_ASSERT(qClearLayer[1] == 8);

    static_assert(sizeof(CmdAckReport) == RawReportSize, "CmdAckReport must be 32 bytes");

    std::cout << "[PASS] Query generation verified\n";
}

void test_report_parsing() {
    KeymapSummaryReport summaryReport{};
    summaryReport.msgType = MsgKeymapSummary;
    summaryReport.layerCount = 6;
    summaryReport.keysPerLayer = 48;
    summaryReport.defaultLayer = 0;
    std::memcpy(summaryReport.buildId, "cd1b94f9", 8);

    TEST_ASSERT(summaryReport.layerCount == 6);
    TEST_ASSERT(summaryReport.keysPerLayer == 48);
    TEST_ASSERT(cleanString(summaryReport.buildId, 8) == "cd1b94f9");

    LayerInfoReport infoReport{};
    infoReport.msgType = MsgLayerInfo;
    infoReport.layerIndex = 1;
    infoReport.layerId = 1;
    infoReport.isActive = 0;
    std::memcpy(infoReport.name, "NUMBER", 6);

    TEST_ASSERT(infoReport.layerIndex == 1);
    TEST_ASSERT(cleanString(infoReport.name, MaxLayerNameLen) == "NUMBER");

    LayerBindingReport bindingReport{};
    bindingReport.msgType = MsgLayerBinding;
    bindingReport.layerIndex = 0;
    bindingReport.bindingIndex = 0;
    std::memcpy(bindingReport.behaviorName, "key_press", 9);
    bindingReport.param1 = 458805;
    bindingReport.param2 = 0;

    TEST_ASSERT(cleanString(bindingReport.behaviorName, MaxBehaviorNameLen) == "key_press");
    TEST_ASSERT(bindingReport.param1 == 458805);

    std::cout << "[PASS] Report parsing verified\n";
}

void test_oryx_protocol() {
    using namespace strata::protocol::oryx;

    static_assert(ReportSize == 32, "Oryx report size must be 32 bytes");

    auto qVer = makeGetProtocolVersionQuery();
    TEST_ASSERT(qVer.size() == 32);
    TEST_ASSERT(qVer[0] == cmd::GetProtocolVersion);
    for (size_t i = 1; i < 32; ++i) {
        TEST_ASSERT(qVer[i] == StopByte);
    }

    auto qPair = makePairingInitQuery();
    TEST_ASSERT(qPair.size() == 32);
    TEST_ASSERT(qPair[0] == cmd::PairingInit);
    TEST_ASSERT(qPair[1] == StopByte);

    auto qLock = makeSetLayerQuery(3, true);
    TEST_ASSERT(qLock[0] == cmd::SetLayer);
    TEST_ASSERT(qLock[1] == 0x01);
    TEST_ASSERT(qLock[2] == 3);
    TEST_ASSERT(qLock[3] == StopByte);

    auto qUnlock = makeSetLayerQuery(3, false);
    TEST_ASSERT(qUnlock[0] == cmd::SetLayer);
    TEST_ASSERT(qUnlock[1] == 0x00);
    TEST_ASSERT(qUnlock[2] == 3);

    auto qLed = makeSetRgbLedQuery(7, 255, 128, 64);
    TEST_ASSERT(qLed[0] == cmd::SetRgbLed);
    TEST_ASSERT(qLed[1] == 7);
    TEST_ASSERT(qLed[2] == 255);
    TEST_ASSERT(qLed[3] == 128);
    TEST_ASSERT(qLed[4] == 64);

    auto qLedAll = makeSetRgbLedAllQuery(10, 20, 30);
    TEST_ASSERT(qLedAll[0] == cmd::SetRgbLedAll);
    TEST_ASSERT(qLedAll[1] == 10);
    TEST_ASSERT(qLedAll[2] == 20);
    TEST_ASSERT(qLedAll[3] == 30);

    auto qBriUp = makeUpdateBrightnessQuery(true);
    TEST_ASSERT(qBriUp[0] == cmd::UpdateBrightness);
    TEST_ASSERT(qBriUp[1] == 0x01);

    auto qBriDown = makeUpdateBrightnessQuery(false);
    TEST_ASSERT(qBriDown[0] == cmd::UpdateBrightness);
    TEST_ASSERT(qBriDown[1] == 0x00);

    std::cout << "[PASS] Oryx protocol query generation verified\n";
}

void test_capabilities() {
    using strata::DeviceCapability;
    using strata::hasCapability;

    DeviceCapability caps =
        DeviceCapability::ActiveLayerNotify | DeviceCapability::ControllableLayer;
    TEST_ASSERT(hasCapability(caps, DeviceCapability::ActiveLayerNotify));
    TEST_ASSERT(hasCapability(caps, DeviceCapability::ControllableLayer));
    TEST_ASSERT(!hasCapability(caps, DeviceCapability::ReadableKeymap));

    caps |= DeviceCapability::LightingControl;
    TEST_ASSERT(hasCapability(caps, DeviceCapability::LightingControl));

    auto strings = strata::capabilitiesToStrings(caps);
    TEST_ASSERT(strings.size() == 3);
    TEST_ASSERT(strings[0] == "active_layer_notify");
    TEST_ASSERT(strings[1] == "controllable_layer");
    TEST_ASSERT(strings[2] == "lighting_control");

    std::cout << "[PASS] Device capabilities verified\n";
}

int main() {
    std::cout << "Running protocol tests...\n";
    test_report_sizes();
    test_query_generation();
    test_report_parsing();
    test_oryx_protocol();
    test_capabilities();
    std::cout << "All protocol tests passed successfully!\n";
    return 0;
}
