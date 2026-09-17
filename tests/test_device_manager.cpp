#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "device/device_manager.hpp"
#include "device/i_device.hpp"
#include "device/oryx_layout.hpp"

#define TEST_ASSERT(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "ASSERTION FAILED: " #cond << " at " << __FILE__ << ":" << __LINE__       \
                      << "\n";                                                                     \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)

namespace strata::device {

class MockDevice : public IDevice {
public:
    MockDevice(std::string id, std::string name, DeviceCapability caps)
        : id_(std::move(id))
        , name_(std::move(name))
        , caps_(caps) {}

    std::string id() const override { return id_; }
    DeviceType type() const override { return DeviceType::ZmkRawHid; }
    std::string name() const override { return name_; }
    std::string deviceNode() const override { return "/dev/hidraw_mock"; }
    std::string buildId() const override { return "mock_build"; }
    DeviceCapability capabilities() const override { return caps_; }
    bool isOpen() const override { return isOpen_; }

    bool open() override {
        isOpen_ = true;
        return true;
    }
    void close() override { isOpen_ = false; }
    void poll(std::chrono::milliseconds /*timeout*/) override {}

    bool setLayer(uint8_t layer, bool lock) override {
        lastLayer_ = layer;
        locked_ = lock;
        return true;
    }

    uint8_t lastLayer_{0};
    bool locked_{false};

private:
    std::string id_;
    std::string name_;
    DeviceCapability caps_;
    bool isOpen_{true};
};

} // namespace strata::device

int main() {
    using namespace strata;
    using namespace strata::device;

    std::cout << "Running device manager unit tests...\n";

    auto dev1 = std::make_unique<MockDevice>("corne-0", "Eyelash Corne",
                                             DeviceCapability::ActiveLayerNotify |
                                                 DeviceCapability::ReadableKeymap);
    auto dev2 = std::make_unique<MockDevice>("voyager-0", "ZSA Voyager",
                                             DeviceCapability::ActiveLayerNotify |
                                                 DeviceCapability::ControllableLayer |
                                                 DeviceCapability::LightingControl);

    TEST_ASSERT(dev1->hasCapability(DeviceCapability::ReadableKeymap));
    TEST_ASSERT(!dev1->hasCapability(DeviceCapability::LightingControl));

    TEST_ASSERT(dev2->hasCapability(DeviceCapability::ControllableLayer));
    TEST_ASSERT(dev2->hasCapability(DeviceCapability::LightingControl));
    TEST_ASSERT(!dev2->hasCapability(DeviceCapability::ReadableKeymap));

    TEST_ASSERT(dev2->setLayer(3, true));
    TEST_ASSERT(dev2->lastLayer_ == 3);
    TEST_ASSERT(dev2->locked_ == true);

    // Test sanitizeId
    TEST_ASSERT(DeviceManager::sanitizeId("corne-f6:46:3b:d4:c4:1a") == "corne_f6_46_3b_d4_c4_1a");
    TEST_ASSERT(DeviceManager::sanitizeId("voyager_123") == "voyager_123");
    TEST_ASSERT(DeviceManager::sanitizeId("") == "device");

    // Test Oryx QMK code to label
    TEST_ASSERT(oryx::qmkCodeToLabel("KC_A") == "A");
    TEST_ASSERT(oryx::qmkCodeToLabel("KC_SPACE") == "SPACE");
    TEST_ASSERT(oryx::qmkCodeToLabel("KC_GRAVE") == "`");
    TEST_ASSERT(oryx::qmkCodeToLabel("KC_ESCAPE") == "ESC");
    TEST_ASSERT(oryx::qmkCodeToLabel("ALL_T") == "HYPER");
    TEST_ASSERT(oryx::qmkCodeToLabel("MEH_T") == "MEH");

    // Test Oryx layout JSON parser
    const std::string mockOryx = R"({
        "data": {
            "layout": {
                "title": "My Voyager",
                "revision": {
                    "hashId": "test_hash",
                    "title": "rev",
                    "layers": [
                        {
                            "title": "Main",
                            "position": 0,
                            "keys": [
                                {"tap": {"code": "KC_Q"}},
                                {"tap": {"code": "KC_A"}, "hold": {"code": "KC_LGUI"}},
                                {"tap": {"code": "KC_SPACE"}, "hold": {"code": "MO", "layer": 1}}
                            ]
                        }
                    ]
                }
            }
        }
    })";

    auto parsed = oryx::parseLayoutJson(mockOryx);
    TEST_ASSERT(parsed.has_value());
    TEST_ASSERT(parsed->layers.size() == 1);
    TEST_ASSERT(parsed->layers[0].name == "Main");
    TEST_ASSERT(parsed->bindings.contains(0));
    TEST_ASSERT(parsed->bindings.at(0).size() == 3);
    TEST_ASSERT(parsed->bindings.at(0)[0].primaryLabel == "Q");
    TEST_ASSERT(parsed->bindings.at(0)[0].category == "alpha");
    TEST_ASSERT(parsed->bindings.at(0)[1].primaryLabel == "A");
    TEST_ASSERT(parsed->bindings.at(0)[1].secondaryLabel == "GUI");
    TEST_ASSERT(parsed->bindings.at(0)[1].category == "mod");
    TEST_ASSERT(parsed->bindings.at(0)[2].primaryLabel == "SPACE");
    TEST_ASSERT(parsed->bindings.at(0)[2].secondaryLabel == "MO(1)");
    TEST_ASSERT(parsed->bindings.at(0)[2].category == "layer");

    std::cout << "[PASS] MockDevice capabilities and commands verified\n";
    std::cout << "[PASS] DeviceManager::sanitizeId verified\n";
    std::cout << "[PASS] Oryx layout parser verified\n";
    std::cout << "All device manager tests passed successfully!\n";
    return 0;
}
