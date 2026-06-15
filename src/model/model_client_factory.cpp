#include "model_client_factory.h"

#include <fstream>
#include <utility>

#include "model_config.h"
#include "provider/anthropic/anthropic_model_client.h"
#include "provider/openai/openai_model_client.h"
#include <nlohmann/json.hpp>

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

OpenAiModelConfig LoadOpenAiConfig(const MemoryModelConfig& source)
{
    OpenAiModelConfig config;
    config.baseUrl = source.baseUrl;
    config.apiKey = source.apiKey;
    config.modelName = source.modelName;
    config.timeoutSeconds = source.timeoutSeconds;
    config.temperature = source.temperature;
    config.maxTokens = source.maxTokens;
    config.headers = source.headers;
    config.extraParams = source.extraParams;
    config.organization = source.organization;
    return config;
}

AnthropicModelConfig LoadAnthropicConfig(const MemoryModelConfig& source)
{
    AnthropicModelConfig config;
    config.baseUrl = source.baseUrl;
    config.apiKey = source.apiKey;
    config.modelName = source.modelName;
    config.timeoutSeconds = source.timeoutSeconds;
    config.temperature = source.temperature;
    config.maxTokens = source.maxTokens == 0 ? 4096 : source.maxTokens;
    config.headers = source.headers;
    config.extraParams = source.extraParams;
    config.anthropicVersion = source.anthropicVersion.empty() ? "2023-06-01" : source.anthropicVersion;
    return config;
}

bool HasInvalidString(const nlohmann::json& j, const std::string& key)
{
    return j.contains(key) && !j[key].is_string();
}

bool HasInvalidNumber(const nlohmann::json& j, const std::string& key)
{
    return j.contains(key) && !j[key].is_number();
}

bool HasInvalidInteger(const nlohmann::json& j, const std::string& key)
{
    return j.contains(key) && !j[key].is_number_integer();
}

ModelClientLoadResult ValidateCommon(const std::string& formatType, const ModelConfig& config)
{
    if (config.baseUrl.empty()) {
        return {nullptr, formatType + " model config missing baseUrl"};
    }
    if (config.modelName.empty()) {
        return {nullptr, formatType + " model config missing modelName"};
    }
    if (config.timeoutSeconds <= 0) {
        return {nullptr, formatType + " model config timeoutSeconds must be positive"};
    }
    if (config.maxTokens < 0) {
        return {nullptr, formatType + " model config maxTokens must be non-negative"};
    }
    if (config.temperature < 0.0 || config.temperature > 2.0) {
        return {nullptr, formatType + " model config temperature must be between 0 and 2"};
    }
    return {nullptr, ""};
}

MemoryModelConfigLoadResult ValidateJsonConfig(const nlohmann::json& j, const std::string& formatType)
{
    if (HasInvalidString(j, "formatType")) {
        return {{}, "model config formatType must be a string"};
    }
    if (HasInvalidString(j, "baseUrl")) {
        return {{}, formatType + " model config baseUrl must be a string"};
    }
    if (HasInvalidString(j, "apiKey")) {
        return {{}, formatType + " model config apiKey must be a string"};
    }
    if (HasInvalidString(j, "organization")) {
        return {{}, formatType + " model config organization must be a string"};
    }
    if (HasInvalidString(j, "anthropicVersion") || HasInvalidString(j, "anthropic-version")) {
        return {{}, formatType + " model config anthropicVersion must be a string"};
    }
    if (HasInvalidString(j, "modelName")) {
        return {{}, formatType + " model config modelName must be a string"};
    }
    if (HasInvalidInteger(j, "timeoutSeconds")) {
        return {{}, formatType + " model config timeoutSeconds must be an integer"};
    }
    if (HasInvalidInteger(j, "maxTokens") || HasInvalidInteger(j, "max_tokens")) {
        return {{}, formatType + " model config maxTokens must be an integer"};
    }
    if (HasInvalidNumber(j, "temperature")) {
        return {{}, formatType + " model config temperature must be a number"};
    }
    if (j.contains("headers") && !j["headers"].is_object()) {
        return {{}, formatType + " model config headers must be an object"};
    }
    if (j.contains("extraParams") && !j["extraParams"].is_object()) {
        return {{}, formatType + " model config extraParams must be an object"};
    }
    return {{}, ""};
}

} // namespace

MemoryModelConfigLoadResult LoadMemoryModelConfigFromJson(const nlohmann::json& j)
{
    if (!j.is_object()) {
        return {{}, "invalid model config JSON"};
    }

    std::string formatType = LoadString(j, "formatType", "openai");
    auto jsonValidation = ValidateJsonConfig(j, formatType);
    if (!jsonValidation.error.empty()) {
        return jsonValidation;
    }

    MemoryModelConfig config;
    config.enabled = true;
    config.formatType = formatType;
    config.baseUrl = LoadString(j, "baseUrl");
    config.apiKey = LoadString(j, "apiKey");
    config.modelName = LoadString(j, "modelName");
    config.organization = LoadString(j, "organization");
    config.anthropicVersion = LoadString(j, "anthropicVersion", LoadString(j, "anthropic-version", config.anthropicVersion));
    config.timeoutSeconds = LoadInt(j, "timeoutSeconds", config.timeoutSeconds);
    config.temperature = LoadDouble(j, "temperature", config.temperature);
    config.maxTokens = LoadInt(j, "maxTokens", LoadInt(j, "max_tokens", config.maxTokens));
    if (j.contains("extraParams") && j["extraParams"].is_object()) {
        config.extraParams = j["extraParams"];
    }
    LoadHeaders(j, config.headers);
    return {config, ""};
}

ModelClientLoadResult LoadModelClientFromConfig(const MemoryModelConfig& modelConfig)
{
    if (!modelConfig.enabled) {
        return {nullptr, ""};
    }

    if (modelConfig.formatType == "openai") {
        OpenAiModelConfig config = LoadOpenAiConfig(modelConfig);
        auto validation = ValidateCommon(modelConfig.formatType, config);
        if (!validation.error.empty()) {
            return validation;
        }
        return {std::make_unique<OpenAiModelClient>(std::move(config)), ""};
    }

    if (modelConfig.formatType == "anthropic") {
        AnthropicModelConfig config = LoadAnthropicConfig(modelConfig);
        auto validation = ValidateCommon(modelConfig.formatType, config);
        if (!validation.error.empty()) {
            return validation;
        }
        return {std::make_unique<AnthropicModelClient>(std::move(config)), ""};
    }

    return {nullptr, "unsupported model formatType: " + modelConfig.formatType};
}

ModelClientLoadResult LoadModelClientFromJson(const nlohmann::json& j)
{
    auto configResult = LoadMemoryModelConfigFromJson(j);
    if (!configResult) {
        return {nullptr, configResult.error};
    }
    return LoadModelClientFromConfig(configResult.config);
}

ModelClientLoadResult LoadModelClientWithResult(const std::string& jsonFile)
{
    std::ifstream file(jsonFile);
    if (!file.is_open()) {
        return {nullptr, "failed to open model config: " + jsonFile};
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return {nullptr, "invalid model config JSON"};
    }

    return LoadModelClientFromJson(j);
}

} // namespace agent_memory
