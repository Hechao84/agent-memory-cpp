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

OpenAiModelConfig LoadOpenAiConfig(const nlohmann::json& j)
{
    OpenAiModelConfig config;
    static_cast<ModelConfig&>(config) = LoadModelConfig(j, 0);
    config.organization = LoadString(j, "organization");
    return config;
}

AnthropicModelConfig LoadAnthropicConfig(const nlohmann::json& j)
{
    AnthropicModelConfig config;
    static_cast<ModelConfig&>(config) = LoadModelConfig(j, 4096);
    config.anthropicVersion = LoadString(j, "anthropicVersion", LoadString(j, "anthropic-version", "2023-06-01"));
    return config;
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

ModelClientLoadResult ValidateCommon(const nlohmann::json& j, const std::string& formatType, const ModelConfig& config)
{
    if (HasInvalidString(j, "baseUrl")) {
        return {nullptr, formatType + " model config baseUrl must be a string"};
    }
    if (HasInvalidString(j, "apiKey")) {
        return {nullptr, formatType + " model config apiKey must be a string"};
    }
    if (HasInvalidString(j, "organization")) {
        return {nullptr, formatType + " model config organization must be a string"};
    }
    if (HasInvalidString(j, "anthropicVersion") || HasInvalidString(j, "anthropic-version")) {
        return {nullptr, formatType + " model config anthropicVersion must be a string"};
    }
    if (HasInvalidString(j, "modelName")) {
        return {nullptr, formatType + " model config modelName must be a string"};
    }
    if (HasInvalidInteger(j, "timeoutSeconds")) {
        return {nullptr, formatType + " model config timeoutSeconds must be an integer"};
    }
    if (HasInvalidInteger(j, "maxTokens") || HasInvalidInteger(j, "max_tokens")) {
        return {nullptr, formatType + " model config maxTokens must be an integer"};
    }
    if (HasInvalidNumber(j, "temperature")) {
        return {nullptr, formatType + " model config temperature must be a number"};
    }
    if (j.contains("headers") && !j["headers"].is_object()) {
        return {nullptr, formatType + " model config headers must be an object"};
    }
    if (j.contains("extraParams") && !j["extraParams"].is_object()) {
        return {nullptr, formatType + " model config extraParams must be an object"};
    }
    return ValidateCommon(formatType, config);
}

} // namespace

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
    if (!j.is_object()) {
        return {nullptr, "invalid model config JSON"};
    }

    if (HasInvalidString(j, "formatType")) {
        return {nullptr, "model config formatType must be a string"};
    }
    std::string formatType = LoadString(j, "formatType", "openai");
    if (formatType == "openai") {
        OpenAiModelConfig config = LoadOpenAiConfig(j);
        auto validation = ValidateCommon(j, formatType, config);
        if (!validation.error.empty()) {
            return validation;
        }
        return {std::make_unique<OpenAiModelClient>(std::move(config)), ""};
    }

    if (formatType == "anthropic") {
        AnthropicModelConfig config = LoadAnthropicConfig(j);
        auto validation = ValidateCommon(j, formatType, config);
        if (!validation.error.empty()) {
            return validation;
        }
        return {std::make_unique<AnthropicModelClient>(std::move(config)), ""};
    }

    return {nullptr, "unsupported model formatType: " + formatType};
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
