#pragma once

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemoryModelConfig
{
    bool enabled{false};
    std::string formatType{"openai"};
    std::string baseUrl;
    std::string apiKey;
    std::string modelName;
    std::string organization;
    std::string anthropicVersion{"2023-06-01"};
    int timeoutSeconds{60};
    double temperature{0.0};
    int maxTokens{0};
    std::unordered_map<std::string, std::string> headers;
    nlohmann::json extraParams = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemoryConfig
{
    std::string dataPath;
    int tokenBudget{4096};
    int offloadThresholdChars{8000};
    bool enablePayloadOffload{false};
    MemoryModelConfig model;
};

} // namespace agent_memory
