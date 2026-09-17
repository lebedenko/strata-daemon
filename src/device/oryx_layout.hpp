#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "common/types.hpp"

namespace strata::oryx {

// Fetches layout from Oryx GraphQL API given a layout hash and revision
std::optional<KeymapData> fetchLayout(std::string_view hashId,
                                      std::string_view revisionId = "latest");

// Parses the GraphQL response JSON into KeymapData
std::optional<KeymapData> parseLayoutJson(const std::string &jsonStr);

// Translates a single Oryx key node into KeyBinding
KeyBinding parseOryxKey(uint8_t pos, const nlohmann::json &keyJson);

// Human-friendly label for a QMK keycode
std::string qmkCodeToLabel(std::string_view code);

// Category classification for layout styling
std::string determineCategory(std::string_view label, std::string_view holdLabel);

} // namespace strata::oryx
