#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"
#include "agent_memory/long_term_memory.h"
#include "agent_memory/payload.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemoryMessage
{
    std::string role;
    std::string content;
    std::string toolCallId;
    std::string toolName;
    std::string payloadRef;
};

struct AGENT_MEMORY_API MemoryContextRequest
{
    std::string agentId;
    std::string sessionId;
    std::string query;
    int tokenBudget{4096};
    std::vector<std::string> includeSections;
    nlohmann::json metadata = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemoryContextPackage
{
    std::vector<MemoryMessage> messages;
    std::string memoryText;
    std::vector<MemoryEntity> entities;
    std::vector<MemoryRelation> relations;
    std::vector<MemoryPayloadRef> payloadRefs;
    std::vector<std::string> citations;
    nlohmann::json metadata = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemoryContextResult
{
    bool succeeded{false};
    MemoryContextPackage context;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
