#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "agent_memory/model_client.h"

namespace agent_memory {

struct ModelClientLoadResult
{
    std::unique_ptr<ModelClient> client;
    std::string error;

    explicit operator bool() const { return client != nullptr; }
};

ModelClientLoadResult LoadModelClientFromJson(const nlohmann::json& j);
ModelClientLoadResult LoadModelClientWithResult(const std::string& jsonFile);

} // namespace agent_memory
