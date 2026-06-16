#pragma once

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "agent_memory/export.h"

namespace agent_memory {

/** Configuration for the runtime-owned OpenAI-compatible or Anthropic-compatible model client. */
struct AGENT_MEMORY_API MemoryModelConfig
{
    /** Enables runtime-owned model loading when true. */
    bool enabled{false};
    /** Provider protocol: openai or anthropic. */
    std::string formatType{"openai"};
    /** Provider base URL. */
    std::string baseUrl;
    /** Provider API key. Keep this out of logs and source control. */
    std::string apiKey;
    /** Provider model name. */
    std::string modelName;
    /** Optional OpenAI organization header. */
    std::string organization;
    /** Optional Anthropic version header. */
    std::string anthropicVersion{"2023-06-01"};
    /** Request timeout in seconds. */
    int timeoutSeconds{60};
    /** Sampling temperature passed through to the provider. */
    double temperature{0.0};
    /** Maximum output tokens. Zero means use the provider adapter default when one exists. */
    int maxTokens{0};
    /** Additional HTTP headers. */
    std::unordered_map<std::string, std::string> headers;
    /** Additional provider request body parameters. */
    nlohmann::json extraParams = nlohmann::json::object();
};

/** Runtime configuration shared by SDK and server-backed runtime setup. */
struct AGENT_MEMORY_API MemoryConfig
{
    /** Runtime data directory. Empty uses the runtime default. */
    std::string dataPath;
    /** Default context token budget. */
    int tokenBudget{4096};
    /** Character threshold for payload file offload. */
    int offloadThresholdChars{8000};
    /** Enables payload file offload for content at or above offloadThresholdChars. */
    bool enablePayloadOffload{true};
    /** Runtime-owned model configuration. */
    MemoryModelConfig model;
};

} // namespace agent_memory
