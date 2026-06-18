#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "agent_memory/config.h"
#include "agent_memory/model_client.h"
#include "model_http_client.h"

namespace agent_memory {

struct MemoryModelConfigLoadResult
{
    MemoryModelConfig config;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

struct ModelClientLoadResult
{
    std::unique_ptr<MemoryModelClient> client;
    std::string error;

    explicit operator bool() const { return client != nullptr; }
};

MemoryModelConfigLoadResult LoadMemoryModelConfigFromJson(const nlohmann::json& j);
ModelClientLoadResult LoadModelClientFromConfig(const MemoryModelConfig& config);
ModelClientLoadResult LoadModelClientFromConfig(const MemoryModelConfig& config, ModelHttpClient httpClient);
ModelClientLoadResult LoadModelClientFromJson(const nlohmann::json& j);
ModelClientLoadResult LoadModelClientWithResult(const std::string& jsonFile);

} // namespace agent_memory
