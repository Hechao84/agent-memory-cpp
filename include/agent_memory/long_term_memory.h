#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemoryEntity
{
    std::string id;
    std::string agentId;
    std::string entityType;
    std::string name;
    std::string summary;
    float confidence{0.0F};
    bool isActive{true};
    std::string supersededByEntityId;
    std::string supersededEntityId;
    std::vector<std::string> sourceRefs;
    nlohmann::json metadata = nlohmann::json::object();
    std::string createdAt;
    std::string updatedAt;
};

struct AGENT_MEMORY_API MemoryRelation
{
    std::string id;
    std::string agentId;
    std::string fromEntityId;
    std::string relationType;
    std::string toEntityId;
    float confidence{0.0F};
    std::vector<std::string> sourceRefs;
    nlohmann::json metadata = nlohmann::json::object();
    std::string createdAt;
    std::string updatedAt;
};

struct AGENT_MEMORY_API MemoryConsolidationRequest
{
    std::string agentId;
    std::string sessionId;
    int maxEvents{100};
    bool forceReprocess{false};
    nlohmann::json metadata = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemoryConsolidationResult
{
    bool succeeded{false};
    bool fallbackUsed{false};
    int processedEvents{0};
    int savedSummaries{0};
    int savedEntities{0};
    int savedRelations{0};
    std::string nextCursor;
    std::string sessionId;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
