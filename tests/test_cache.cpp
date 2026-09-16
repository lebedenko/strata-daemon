#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "cache/keymap_cache.hpp"

#define TEST_ASSERT(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "ASSERTION FAILED: " #cond << " at " << __FILE__ << ":" << __LINE__       \
                      << "\n";                                                                     \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)

namespace fs = std::filesystem;

int main() {
    std::cout << "Running cache tests...\n";

    fs::path test_dir = fs::temp_directory_path() / "strata_test_cache";
    fs::remove_all(test_dir);

    strata::KeymapCache cache(test_dir);
    TEST_ASSERT(cache.cache_dir() == test_dir);

    // Prepare mock KeymapData
    strata::KeymapData data;
    data.deviceName = "eyelash_corne";
    data.buildId = "abcd1234";
    data.summary.layerCount = 2;
    data.summary.keysPerLayer = 48;
    data.summary.defaultLayer = 0;
    data.summary.buildId = "abcd1234";

    strata::LayerInfo l0{0, 0, "QWERTY", true};
    strata::LayerInfo l1{1, 1, "NUMBER", false};
    data.layers = {l0, l1};

    strata::KeyBinding b0{0, "key_press", 458805, 0};
    strata::KeyBinding b1{1, "key_press", 458772, 0};
    data.bindings[0] = {b0, b1};

    // Test save
    bool saved = cache.save(data);
    TEST_ASSERT(saved);
    std::cout << "[PASS] Save keymap data\n";

    // Test load
    auto loaded = cache.load("eyelash_corne", "abcd1234");
    TEST_ASSERT(loaded.has_value());
    TEST_ASSERT(loaded->deviceName == "eyelash_corne");
    TEST_ASSERT(loaded->buildId == "abcd1234");
    TEST_ASSERT(loaded->summary.layerCount == 2);
    TEST_ASSERT(loaded->summary.keysPerLayer == 48);
    TEST_ASSERT(loaded->layers.size() == 2);
    TEST_ASSERT(loaded->layers[0].name == "QWERTY");
    TEST_ASSERT(loaded->layers[1].name == "NUMBER");
    TEST_ASSERT(loaded->bindings.contains(0));
    TEST_ASSERT(loaded->bindings.at(0).size() == 2);
    TEST_ASSERT(loaded->bindings.at(0)[0].behavior == "key_press");
    TEST_ASSERT(loaded->bindings.at(0)[0].param1 == 458805);
    std::cout << "[PASS] Load keymap data\n";

    // Test list_cached_builds
    auto builds = cache.list_cached_builds("eyelash_corne");
    TEST_ASSERT(builds.size() == 1);
    TEST_ASSERT(builds[0] == "abcd1234");
    std::cout << "[PASS] List cached builds\n";

    // Test update_bindings
    strata::KeyBinding b2{0, "to_layer", 1, 0};
    bool updated = cache.update_bindings("eyelash_corne", "abcd1234", 1, {b2});
    TEST_ASSERT(updated);

    auto reloaded = cache.load("eyelash_corne", "abcd1234");
    TEST_ASSERT(reloaded.has_value());
    TEST_ASSERT(reloaded->bindings.contains(1));
    TEST_ASSERT(reloaded->bindings.at(1).size() == 1);
    TEST_ASSERT(reloaded->bindings.at(1)[0].behavior == "to_layer");
    std::cout << "[PASS] Update layer bindings\n";

    // Test clear
    bool cleared = cache.clear();
    TEST_ASSERT(cleared);
    auto empty_builds = cache.list_cached_builds("eyelash_corne");
    TEST_ASSERT(empty_builds.empty());
    std::cout << "[PASS] Clear cache\n";

    fs::remove_all(test_dir);
    std::cout << "All cache tests passed successfully!\n";
    return 0;
}
