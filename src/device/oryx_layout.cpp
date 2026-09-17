#include "device/oryx_layout.hpp"

#include <cctype>
#include <curl/curl.h>
#include <iostream>

#include "common/logger.hpp"

namespace strata::oryx {

namespace {

size_t curlWriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    auto *str = static_cast<std::string *>(userp);
    str->append(static_cast<char *>(contents), total);
    return total;
}

} // namespace

std::string qmkCodeToLabel(std::string_view code) {
    if (code.starts_with("KC_")) {
        code.remove_prefix(3);
    }
    if (code == "GRAVE")
        return "`";
    if (code == "TILD")
        return "~";
    if (code == "EXLM")
        return "!";
    if (code == "AT")
        return "@";
    if (code == "HASH")
        return "#";
    if (code == "DLR")
        return "$";
    if (code == "PERC")
        return "%";
    if (code == "CIRC")
        return "^";
    if (code == "AMPR")
        return "&";
    if (code == "ASTR")
        return "*";
    if (code == "LPRN")
        return "(";
    if (code == "RPRN")
        return ")";
    if (code == "MINUS")
        return "-";
    if (code == "UNDS")
        return "_";
    if (code == "EQUAL")
        return "=";
    if (code == "PLUS")
        return "+";
    if (code == "LBRACKET")
        return "[";
    if (code == "RBRACKET")
        return "]";
    if (code == "LCBR")
        return "{";
    if (code == "RCBR")
        return "}";
    if (code == "BSLASH")
        return "\\";
    if (code == "SLASH")
        return "/";
    if (code == "SCOLON")
        return ";";
    if (code == "COLON")
        return ":";
    if (code == "QUOTE")
        return "'";
    if (code == "DQUO")
        return "\"";
    if (code == "COMMA")
        return ",";
    if (code == "DOT")
        return ".";
    if (code == "SPACE")
        return "SPACE";
    if (code == "TAB")
        return "TAB";
    if (code == "ENTER")
        return "ENTER";
    if (code == "ESCAPE")
        return "ESC";
    if (code == "BSPC" || code == "BSPACE")
        return "BSPC";
    if (code == "DELETE" || code == "DEL")
        return "DEL";
    if (code == "HOME")
        return "HOME";
    if (code == "END")
        return "END";
    if (code == "PGUP")
        return "PGUP";
    if (code == "PGDOWN")
        return "PGDN";
    if (code == "LEFT")
        return "LEFT";
    if (code == "RIGHT")
        return "RIGHT";
    if (code == "UP")
        return "UP";
    if (code == "DOWN")
        return "DOWN";
    if (code == "LCTRL" || code == "RCTRL")
        return "CTRL";
    if (code == "LSHIFT" || code == "RSHIFT" || code == "LEFT_SHIFT")
        return "SHIFT";
    if (code == "LALT" || code == "RALT")
        return "ALT";
    if (code == "LGUI" || code == "RGUI")
        return "GUI";
    if (code == "MEH_T")
        return "MEH";
    if (code == "ALL_T")
        return "HYPER";
    if (code == "TRANSPARENT")
        return "▽";
    if (code == "NO")
        return "---";
    if (code == "AUDIO_MUTE")
        return "MUTE";
    if (code == "AUDIO_VOL_UP")
        return "VOL+";
    if (code == "AUDIO_VOL_DOWN")
        return "VOL-";
    if (code == "MEDIA_PLAY_PAUSE")
        return "PLAY";
    if (code == "MEDIA_NEXT_TRACK")
        return "NEXT";
    if (code == "MEDIA_PREV_TRACK")
        return "PREV";
    if (code == "RGB_TOG")
        return "RGB";
    if (code == "RGB_VAI")
        return "BRI+";
    if (code == "RGB_VAD")
        return "BRI-";
    if (code == "RGB_MOD")
        return "EFFECT";
    if (code == "CAPS_WORD")
        return "CAPS";
    if (code == "CALCULATOR")
        return "CALC";
    if (code == "PSCREEN")
        return "PRTSC";
    if (code == "SYSTEM_POWER")
        return "PWR";
    if (code == "SYSTEM_SLEEP")
        return "SLEEP";
    return std::string(code);
}

std::string determineCategory(std::string_view label, std::string_view holdLabel) {
    if (!holdLabel.empty()) {
        if (holdLabel.starts_with("MO") || holdLabel.starts_with("TG") ||
            holdLabel.starts_with("TO")) {
            return "layer";
        }
        return "mod";
    }
    if (label.starts_with("MO") || label.starts_with("TG") || label.starts_with("TO")) {
        return "layer";
    }
    if (label == "CTRL" || label == "SHIFT" || label == "ALT" || label == "GUI" || label == "MEH" ||
        label == "HYPER") {
        return "mod";
    }
    if (label.length() == 1) {
        char c = label[0];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            return "alpha";
        if (c >= '0' && c <= '9')
            return "number";
    }
    if (label.starts_with("F") && label.length() >= 2 &&
        std::isdigit(static_cast<unsigned char>(label[1]))) {
        return "fn";
    }
    if (label == "UP" || label == "DOWN" || label == "LEFT" || label == "RIGHT" ||
        label == "HOME" || label == "END" || label == "PGUP" || label == "PGDN") {
        return "nav";
    }
    if (label == "MUTE" || label == "VOL+" || label == "VOL-" || label == "PLAY" ||
        label == "NEXT" || label == "PREV") {
        return "media";
    }
    return "misc";
}

KeyBinding parseOryxKey(uint8_t pos, const nlohmann::json &keyJson) {
    KeyBinding kb;
    kb.pos = pos;

    std::string customLabel;
    if (keyJson.contains("customLabel") && !keyJson["customLabel"].is_null()) {
        customLabel = keyJson["customLabel"].get<std::string>();
    }

    std::string tapCode;
    std::optional<int> tapLayer;
    if (keyJson.contains("tap") && !keyJson["tap"].is_null()) {
        const auto &tap = keyJson["tap"];
        if (tap.contains("code") && !tap["code"].is_null()) {
            tapCode = tap["code"].get<std::string>();
        }
        if (tap.contains("layer") && !tap["layer"].is_null()) {
            tapLayer = tap["layer"].get<int>();
        }
    }

    std::string holdCode;
    std::optional<int> holdLayer;
    if (keyJson.contains("hold") && !keyJson["hold"].is_null()) {
        const auto &hold = keyJson["hold"];
        if (hold.contains("code") && !hold["code"].is_null()) {
            holdCode = hold["code"].get<std::string>();
        }
        if (hold.contains("layer") && !hold["layer"].is_null()) {
            holdLayer = hold["layer"].get<int>();
        }
    }

    std::string doubleTapCode;
    if (keyJson.contains("doubleTap") && !keyJson["doubleTap"].is_null()) {
        const auto &dt = keyJson["doubleTap"];
        if (dt.contains("code") && !dt["code"].is_null()) {
            doubleTapCode = dt["code"].get<std::string>();
        }
    }

    // Determine primary label
    if (!customLabel.empty()) {
        kb.primaryLabel = customLabel;
    } else if (tapLayer.has_value()) {
        std::string prefix = tapCode.empty() ? "TO" : qmkCodeToLabel(tapCode);
        kb.primaryLabel = prefix + "(" + std::to_string(*tapLayer) + ")";
    } else if (!tapCode.empty()) {
        kb.primaryLabel = qmkCodeToLabel(tapCode);
    } else if (holdLayer.has_value() || !holdCode.empty()) {
        kb.primaryLabel = "";
    } else {
        kb.primaryLabel = "---";
    }

    // Determine secondary label
    if (holdLayer.has_value()) {
        std::string prefix = holdCode.empty() ? "MO" : qmkCodeToLabel(holdCode);
        kb.secondaryLabel = prefix + "(" + std::to_string(*holdLayer) + ")";
    } else if (!holdCode.empty()) {
        kb.secondaryLabel = qmkCodeToLabel(holdCode);
    } else if (!doubleTapCode.empty()) {
        kb.secondaryLabel = qmkCodeToLabel(doubleTapCode);
    }

    // Behavior representation
    if (holdLayer.has_value() || !holdCode.empty()) {
        kb.behavior = holdLayer.has_value() ? "mo" : "mt";
    } else if (tapLayer.has_value()) {
        kb.behavior = "to";
    } else if (tapCode == "KC_TRANSPARENT") {
        kb.behavior = "trans";
    } else if (tapCode == "KC_NO" || tapCode.empty()) {
        kb.behavior = "none";
    } else {
        kb.behavior = "kp";
    }

    kb.category = determineCategory(kb.primaryLabel, kb.secondaryLabel);

    if (!kb.secondaryLabel.empty()) {
        kb.tooltip = "Tap: " + kb.primaryLabel + ", Hold: " + kb.secondaryLabel;
    } else {
        kb.tooltip = kb.primaryLabel;
    }

    return kb;
}

std::optional<KeymapData> parseLayoutJson(const std::string &jsonStr) {
    try {
        auto doc = nlohmann::json::parse(jsonStr);
        if (!doc.contains("data") || !doc["data"].contains("layout") ||
            !doc["data"]["layout"].contains("revision") ||
            !doc["data"]["layout"]["revision"].contains("layers")) {
            LOG_ERROR("Invalid Oryx GraphQL response structure");
            return std::nullopt;
        }

        const auto &rev = doc["data"]["layout"]["revision"];
        std::string hashId = rev.value("hashId", "");
        const auto &layersArr = rev["layers"];

        KeymapData data;
        data.deviceName = "ZSA Voyager";
        data.buildId = hashId;
        data.summary.keysPerLayer = 52;
        data.summary.layerCount = static_cast<uint8_t>(layersArr.size());
        data.summary.defaultLayer = 0;
        data.summary.sensorsPerLayer = 0;
        data.summary.buildId = hashId;

        for (const auto &lJson : layersArr) {
            uint8_t pos = static_cast<uint8_t>(lJson.value("position", 0));
            std::string title = lJson.value("title", "Layer " + std::to_string(pos));

            LayerInfo info;
            info.index = pos;
            info.id = pos;
            info.name = title;
            info.isActive = (pos == 0);
            data.layers.push_back(info);

            std::vector<KeyBinding> bindings;
            if (lJson.contains("keys") && lJson["keys"].is_array()) {
                const auto &keysArr = lJson["keys"];
                uint8_t kPos = 0;
                for (const auto &kJson : keysArr) {
                    bindings.push_back(parseOryxKey(kPos++, kJson));
                }
            }
            data.bindings[pos] = std::move(bindings);
        }

        LOG_INFO("Parsed Oryx layout: {} layers, buildId={}", data.layers.size(), data.buildId);
        return data;
    } catch (const std::exception &ex) {
        LOG_ERROR("JSON parse error parsing Oryx layout: {}", ex.what());
        return std::nullopt;
    }
}

std::optional<KeymapData> fetchLayout(std::string_view hashId, std::string_view revisionId) {
    if (hashId.empty()) {
        return std::nullopt;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize libcurl");
        return std::nullopt;
    }

    nlohmann::json payload;
    payload["query"] =
        "query getLayout($hashId: String!, $revisionId: String!, $geometry: String) { "
        "layout(hashId: $hashId, geometry: $geometry, revisionId: $revisionId) { "
        "title revision { hashId title layers { title position keys } } } }";
    payload["variables"] = {{"hashId", std::string(hashId)},
                            {"revisionId", revisionId.empty() ? "latest" : std::string(revisionId)},
                            {"geometry", "voyager"}};

    std::string requestBody = payload.dump();
    std::string responseBody;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, "https://oryx.zsa.io/graphql");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_WARN("libcurl request failed for hash {}: {}", hashId, curl_easy_strerror(res));
        return std::nullopt;
    }

    if (httpCode != 200) {
        LOG_WARN("Oryx GraphQL API returned HTTP {}: {}", httpCode, responseBody);
        return std::nullopt;
    }

    return parseLayoutJson(responseBody);
}

} // namespace strata::oryx
