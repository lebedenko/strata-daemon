#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.hpp"

namespace strata {

class KeymapCache {
public:
    explicit KeymapCache(std::filesystem::path cache_dir = {});

    [[nodiscard]] std::optional<KeymapData> load(std::string_view device_name,
                                                 std::string_view build_id) const;

    bool save(const KeymapData &data) const;

    bool update_bindings(std::string_view device_name, std::string_view build_id, uint8_t layer_idx,
                         const std::vector<KeyBinding> &bindings) const;

    bool update_sensor_bindings(std::string_view device_name, std::string_view build_id,
                                uint8_t layer_idx,
                                const std::vector<SensorBinding> &sensor_bindings) const;

    bool clear() const;

    [[nodiscard]] std::vector<std::string>
    list_cached_builds(std::string_view device_name = "") const;

    [[nodiscard]] std::filesystem::path get_cache_path(std::string_view device_name,
                                                       std::string_view build_id) const;

    [[nodiscard]] const std::filesystem::path &cache_dir() const noexcept { return cache_dir_; }

private:
    std::filesystem::path cache_dir_;

    static std::filesystem::path resolve_default_cache_dir();
};

} // namespace strata
