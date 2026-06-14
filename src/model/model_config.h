#pragma once

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace agent_memory {

struct ModelConfig
{
    std::string baseUrl;
    std::string apiKey;
    std::string modelName;
    int timeoutSeconds{60};
    double temperature{0.0};
    int maxTokens{0};
    std::unordered_map<std::string, std::string> headers;
    nlohmann::json extraParams = nlohmann::json::object();
};

ModelConfig LoadModelConfig(const nlohmann::json& j, int defaultMaxTokens);

} // namespace agent_memory
