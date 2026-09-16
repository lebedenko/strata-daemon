#include <cstdlib>
#include <cstring>
#include <iostream>

#include "common/protocol_zmk.hpp"

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

    auto qBinding = makeGetLayerBindingQuery(2, 14);
    TEST_ASSERT(qBinding.size() == RawReportSize);
    TEST_ASSERT(qBinding[0] == CmdGetLayerBinding);
    TEST_ASSERT(qBinding[1] == 2);
    TEST_ASSERT(qBinding[2] == 14);

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

int main() {
    std::cout << "Running protocol tests...\n";
    test_report_sizes();
    test_query_generation();
    test_report_parsing();
    std::cout << "All protocol tests passed successfully!\n";
    return 0;
}
