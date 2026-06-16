#pragma once

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace agent_memory {

inline const nlohmann::json& JsonFieldValue(const nlohmann::json& j, const std::string& key)
{
    static const nlohmann::json empty;
    if (!j.is_object() || !j.contains(key)) {
        return empty;
    }
    return j[key];
}

inline std::string JsonString(const nlohmann::json& j, const std::string& key, const std::string& fallback = "")
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) {
        return fallback;
    }
    return j[key].get<std::string>();
}

inline int JsonInt(const nlohmann::json& j, const std::string& key, int fallback = 0)
{
    if (!j.is_object() || !j.contains(key)) {
        return fallback;
    }
    const auto& value = j[key];
    if (value.is_number_integer()) {
        const auto number = value.get<long long>();
        if (number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max()) {
            return static_cast<int>(number);
        }
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<unsigned long long>();
        if (number <= static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            return static_cast<int>(number);
        }
    }
    return fallback;
}

inline float JsonFloat(const nlohmann::json& j, const std::string& key, float fallback = 0.0F)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) {
        return fallback;
    }
    return j[key].get<float>();
}

inline double JsonDouble(const nlohmann::json& j, const std::string& key, double fallback = 0.0)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) {
        return fallback;
    }
    return j[key].get<double>();
}

inline bool JsonBool(const nlohmann::json& j, const std::string& key, bool fallback = false)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) {
        return fallback;
    }
    return j[key].get<bool>();
}

inline std::unordered_map<std::string, std::string> JsonStringMap(const nlohmann::json& j, const std::string& key)
{
    std::unordered_map<std::string, std::string> values;
    if (!j.is_object() || !j.contains(key) || !j[key].is_object()) {
        return values;
    }
    for (auto it = j[key].begin(); it != j[key].end(); ++it) {
        if (it.value().is_string()) {
            values[it.key()] = it.value().get<std::string>();
        }
    }
    return values;
}

inline bool HasInvalidString(const nlohmann::json& j, const std::string& key)
{
    return j.is_object() && j.contains(key) && !j[key].is_string();
}

inline bool HasInvalidInteger(const nlohmann::json& j, const std::string& key)
{
    return j.is_object() && j.contains(key) && !j[key].is_number_integer();
}

inline bool HasInvalidNumber(const nlohmann::json& j, const std::string& key)
{
    return j.is_object() && j.contains(key) && !j[key].is_number();
}

inline bool HasInvalidObject(const nlohmann::json& j, const std::string& key)
{
    return j.is_object() && j.contains(key) && !j[key].is_object();
}

inline nlohmann::json JsonObject(const nlohmann::json& value)
{
    return value.is_object() ? value : nlohmann::json::object();
}

inline std::vector<std::string> StringVectorFromJson(const nlohmann::json& j)
{
    std::vector<std::string> values;
    if (!j.is_array()) {
        return values;
    }
    values.reserve(j.size());
    for (const auto& item : j) {
        if (item.is_string()) {
            values.push_back(item.get<std::string>());
        }
    }
    return values;
}

inline std::string MetadataString(const nlohmann::json& metadata, const std::string& key, const std::string& fallback = "")
{
    return JsonString(metadata, key, fallback);
}

inline int MetadataInt(const nlohmann::json& metadata, const std::string& key, int fallback = 0)
{
    int result = JsonInt(metadata, key, fallback);
    if (result != fallback) {
        return result;
    }
    if (!metadata.is_object() || !metadata.contains(key)) {
        return fallback;
    }
    const auto& value = metadata[key];
    try {
        if (value.is_string()) {
            return std::stoi(value.get<std::string>());
        }
    } catch (...) {
    }
    return fallback;
}

} // namespace agent_memory