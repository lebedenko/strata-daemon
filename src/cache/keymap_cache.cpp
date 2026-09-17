#include "cache/keymap_cache.hpp"

#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

#include "common/logger.hpp"

namespace strata {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

KeymapCache::KeymapCache(fs::path cache_dir)
    : cache_dir_(cache_dir.empty() ? resolve_default_cache_dir() : std::move(cache_dir)) {
    std::error_code ec;
    fs::create_directories(cache_dir_, ec);
    if (ec) {
        LOG_WARN("Failed to create cache directory {}: {}", cache_dir_.string(), ec.message());
    } else {
        LOG_DEBUG("Keymap cache directory: {}", cache_dir_.string());
    }
}

fs::path KeymapCache::resolve_default_cache_dir() {
    if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "strata" / "keymaps";
    }
    if (const char *home = std::getenv("HOME"); home && *home) {
        return fs::path(home) / ".cache" / "strata" / "keymaps";
    }
    return fs::temp_directory_path() / "strata" / "keymaps";
}

fs::path KeymapCache::get_cache_path(std::string_view device_name,
                                     std::string_view build_id) const {
    std::string filename = std::string(device_name) + "_" + std::string(build_id) + ".json";
    return cache_dir_ / filename;
}

std::optional<KeymapData> KeymapCache::load(std::string_view device_name,
                                            std::string_view build_id) const {
    if (device_name.empty() || build_id.empty()) {
        return std::nullopt;
    }

    auto path = get_cache_path(device_name, build_id);
    if (!fs::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("Failed to open cache file for reading: {}", path.string());
        return std::nullopt;
    }

    try {
        json j;
        file >> j;

        KeymapData data;
        data.deviceName = j.value("device", std::string(device_name));
        data.buildId = j.value("build_id", std::string(build_id));

        if (j.contains("summary") && j["summary"].is_object()) {
            const auto &s = j["summary"];
            data.summary.layerCount = s.value("layer_count", static_cast<uint8_t>(0));
            data.summary.keysPerLayer = s.value("keys_per_layer", static_cast<uint8_t>(0));
            data.summary.defaultLayer = s.value("default_layer", static_cast<uint8_t>(0));
            data.summary.buildId = s.value("build_id", data.buildId);
        }

        if (j.contains("layers") && j["layers"].is_array()) {
            for (const auto &item : j["layers"]) {
                LayerInfo l;
                l.index = item.value("index", static_cast<uint8_t>(0));
                l.id = item.value("id", static_cast<uint8_t>(0));
                l.name = item.value("name", "");
                l.isActive = item.value("active", false);
                data.layers.push_back(std::move(l));
            }
        }

        if (j.contains("bindings") && j["bindings"].is_object()) {
            for (auto it = j["bindings"].begin(); it != j["bindings"].end(); ++it) {
                try {
                    uint8_t layer_idx = static_cast<uint8_t>(std::stoul(it.key()));
                    std::vector<KeyBinding> b_list;
                    if (it.value().is_array()) {
                        for (const auto &b_item : it.value()) {
                            KeyBinding kb;
                            kb.pos = b_item.value("pos", static_cast<uint8_t>(0));
                            kb.behavior = b_item.value("behavior", "");
                            kb.param1 = b_item.value("param1", static_cast<uint32_t>(0));
                            kb.param2 = b_item.value("param2", static_cast<uint32_t>(0));
                            kb.primaryLabel = b_item.value("primaryLabel", "");
                            kb.secondaryLabel = b_item.value("secondaryLabel", "");
                            kb.category = b_item.value("category", "");
                            kb.tooltip = b_item.value("tooltip", "");
                            b_list.push_back(std::move(kb));
                        }
                    }
                    data.bindings[layer_idx] = std::move(b_list);
                } catch (const std::exception &ex) {
                    LOG_WARN("Failed to parse layer bindings key {}: {}", it.key(), ex.what());
                }
            }
        }

        if (j.contains("sensor_bindings") && j["sensor_bindings"].is_object()) {
            for (auto it = j["sensor_bindings"].begin(); it != j["sensor_bindings"].end(); ++it) {
                try {
                    uint8_t layer_idx = static_cast<uint8_t>(std::stoul(it.key()));
                    std::vector<SensorBinding> s_list;
                    if (it.value().is_array()) {
                        for (const auto &s_item : it.value()) {
                            SensorBinding sb;
                            sb.sensorIndex = s_item.value("sensor", static_cast<uint8_t>(0));
                            sb.behavior = s_item.value("behavior", "");
                            sb.param1 = s_item.value("param1", static_cast<uint32_t>(0));
                            sb.param2 = s_item.value("param2", static_cast<uint32_t>(0));
                            s_list.push_back(std::move(sb));
                        }
                    }
                    data.sensorBindings[layer_idx] = std::move(s_list);
                } catch (const std::exception &ex) {
                    LOG_WARN("Failed to parse sensor bindings key {}: {}", it.key(), ex.what());
                }
            }
        }

        LOG_DEBUG("Successfully loaded cached keymap from {}", path.string());
        return data;
    } catch (const std::exception &ex) {
        LOG_ERROR("JSON parse error reading {}: {}", path.string(), ex.what());
        return std::nullopt;
    }
}

bool KeymapCache::save(const KeymapData &data) const {
    if (data.deviceName.empty() || data.buildId.empty()) {
        LOG_WARN("Cannot save keymap cache: deviceName or buildId empty");
        return false;
    }

    auto target_path = get_cache_path(data.deviceName, data.buildId);
    auto tmp_path = target_path;
    tmp_path += ".tmp";

    try {
        json j;
        j["build_id"] = data.buildId;
        j["device"] = data.deviceName;
        j["summary"] = {{"layer_count", data.summary.layerCount},
                        {"keys_per_layer", data.summary.keysPerLayer},
                        {"default_layer", data.summary.defaultLayer},
                        {"build_id", data.summary.buildId}};

        json layers_arr = json::array();
        for (const auto &l : data.layers) {
            layers_arr.push_back(json::object(
                {{"index", l.index}, {"id", l.id}, {"name", l.name}, {"active", l.isActive}}));
        }
        j["layers"] = layers_arr;

        json bindings_obj = json::object();
        for (const auto &[layer_idx, b_list] : data.bindings) {
            json b_arr = json::array();
            for (const auto &b : b_list) {
                json b_obj = {{"pos", b.pos},
                              {"behavior", b.behavior},
                              {"param1", b.param1},
                              {"param2", b.param2}};
                if (!b.primaryLabel.empty())
                    b_obj["primaryLabel"] = b.primaryLabel;
                if (!b.secondaryLabel.empty())
                    b_obj["secondaryLabel"] = b.secondaryLabel;
                if (!b.category.empty())
                    b_obj["category"] = b.category;
                if (!b.tooltip.empty())
                    b_obj["tooltip"] = b.tooltip;
                b_arr.push_back(std::move(b_obj));
            }
            bindings_obj[std::to_string(layer_idx)] = b_arr;
        }
        j["bindings"] = bindings_obj;

        json sensor_bindings_obj = json::object();
        for (const auto &[layer_idx, s_list] : data.sensorBindings) {
            json s_arr = json::array();
            for (const auto &s : s_list) {
                s_arr.push_back(json::object({{"sensor", s.sensorIndex},
                                              {"behavior", s.behavior},
                                              {"param1", s.param1},
                                              {"param2", s.param2}}));
            }
            sensor_bindings_obj[std::to_string(layer_idx)] = s_arr;
        }
        j["sensor_bindings"] = sensor_bindings_obj;

        {
            std::ofstream out(tmp_path);
            if (!out.is_open()) {
                LOG_ERROR("Failed to open temp cache file for writing: {}", tmp_path.string());
                return false;
            }
            out << j.dump(2) << "\n";
        }

        std::error_code ec;
        fs::rename(tmp_path, target_path, ec);
        if (ec) {
            LOG_ERROR("Failed to rename temp cache file {} to {}: {}", tmp_path.string(),
                      target_path.string(), ec.message());
            fs::remove(tmp_path, ec);
            return false;
        }

        LOG_INFO("Saved keymap cache to {}", target_path.string());
        return true;
    } catch (const std::exception &ex) {
        LOG_ERROR("Exception saving keymap cache: {}", ex.what());
        return false;
    }
}

bool KeymapCache::update_bindings(std::string_view device_name, std::string_view build_id,
                                  uint8_t layer_idx,
                                  const std::vector<KeyBinding> &bindings) const {
    auto data = load(device_name, build_id);
    if (!data) {
        LOG_WARN("Cannot update bindings: no existing cache for {} / {}", device_name, build_id);
        return false;
    }

    data->bindings[layer_idx] = bindings;
    return save(*data);
}

bool KeymapCache::update_sensor_bindings(std::string_view device_name, std::string_view build_id,
                                         uint8_t layer_idx,
                                         const std::vector<SensorBinding> &sensor_bindings) const {
    auto data = load(device_name, build_id);
    if (!data) {
        LOG_WARN("Cannot update sensor bindings: no existing cache for {} / {}", device_name,
                 build_id);
        return false;
    }

    data->sensorBindings[layer_idx] = sensor_bindings;
    return save(*data);
}

bool KeymapCache::clear() const {
    std::error_code ec;
    if (!fs::exists(cache_dir_, ec)) {
        return true;
    }

    bool success = true;
    for (const auto &entry : fs::directory_iterator(cache_dir_, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::error_code rm_ec;
            fs::remove(entry.path(), rm_ec);
            if (rm_ec) {
                LOG_WARN("Failed to remove {}: {}", entry.path().string(), rm_ec.message());
                success = false;
            }
        }
    }
    LOG_INFO("Keymap cache cleared in {}", cache_dir_.string());
    return success;
}

bool KeymapCache::remove(std::string_view device_name, std::string_view build_id) const {
    auto path = get_cache_path(device_name, build_id);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return true;
    }
    bool ok = fs::remove(path, ec);
    if (ok) {
        LOG_INFO("Removed cached keymap: {}", path.string());
    } else {
        LOG_WARN("Failed to remove cached keymap {}: {}", path.string(), ec.message());
    }
    return ok;
}

std::vector<std::string> KeymapCache::list_cached_builds(std::string_view device_name) const {
    std::vector<std::string> result;
    std::error_code ec;
    if (!fs::exists(cache_dir_, ec)) {
        return result;
    }

    for (const auto &entry : fs::directory_iterator(cache_dir_, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string stem = entry.path().stem().string();
            if (device_name.empty()) {
                result.push_back(stem);
            } else {
                std::string prefix = std::string(device_name) + "_";
                if (stem.starts_with(prefix)) {
                    result.push_back(stem.substr(prefix.length()));
                }
            }
        }
    }
    return result;
}

} // namespace strata
