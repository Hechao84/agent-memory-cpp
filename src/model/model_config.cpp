#include "model_config.h"

namespace agent_memory {

namespace {

std::string LoadString(const nlohmann::json& j, const std::string& key, const std::string& defaultValue = "")
{
    if (!j.contains(key) || !j[key].is_string()) {
        return defaultValue;
    }
    return j[key].get<std::string>();
}

int LoadInt(const nlohmann::json& j, const std::string& key, int defaultValue)
{
    if (!j.contains(key) || !j[key].is_number_integer()) {
        return defaultValue;
    }
    return j[key].get<int>();
}

double LoadDouble(const nlohmann::json& j, const std::string& key, double defaultValue)
{
    if (!j.contains(key) || !j[key].is_number()) {
        return defaultValue;
    }
    return j[key].get<double>();
}

void LoadHeaders(const nlohmann::json& j, std::unordered_map<std::string, std::string>& headers)
{
    if (!j.contains("headers") || !j["headers"].is_object()) {
        return;
    }
    for (auto it = j["headers"].begin(); it != j["headers"].end(); ++it) {
        if (it.value().is_string()) {
            headers[it.key()] = it.value().get<std::string>();
        }
    }
}

} // namespace

ModelConfig LoadModelConfig(const nlohmann::json& j, int defaultMaxTokens)
{
    ModelConfig config;
    config.baseUrl = LoadString(j, "baseUrl");
    config.apiKey = LoadString(j, "apiKey");
    config.modelName = LoadString(j, "modelName");
    config.timeoutSeconds = LoadInt(j, "timeoutSeconds", 60);
    config.temperature = LoadDouble(j, "temperature", 0.0);
    config.maxTokens = LoadInt(j, "maxTokens", LoadInt(j, "max_tokens", defaultMaxTokens));
    if (j.contains("extraParams") && j["extraParams"].is_object()) {
        config.extraParams = j["extraParams"];
    }
    LoadHeaders(j, config.headers);
    return config;
}

} // namespace agent_memory
