#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"
#include "agent_memory/long_term_memory.h"
#include "agent_memory/payload.h"

namespace agent_memory {

/** Message entry included in a context package. */
struct AGENT_MEMORY_API MemoryMessage
{
    /** Message role. */
    std::string role;
    /** Message content. */
    std::string content;
    /** Optional tool call id. */
    std::string toolCallId;
    /** Optional tool name. */
    std::string toolName;
    /** Optional payload URI reference. */
    std::string payloadRef;
};

/** Request for building an agent context package. */
struct AGENT_MEMORY_API MemoryContextRequest
{
    /** Agent namespace to read. */
    std::string agentId;
    /** Session namespace to read. */
    std::string sessionId;
    /** Current user/task query used for memory and payload filtering. */
    std::string query;
    /** Token budget used to derive long-term memory defaults. */
    int tokenBudget{4096};
    /** Sections to include; empty means all sections. */
    std::vector<std::string> includeSections;
    /** Optional controls such as message_limit, payload_limit, and long_term_limit. */
    nlohmann::json metadata = nlohmann::json::object();
};

/** Context package assembled from recent messages, long-term memory, and payload refs. */
struct AGENT_MEMORY_API MemoryContextPackage
{
    /** Recent messages selected for context. */
    std::vector<MemoryMessage> messages;
    /** Rendered long-term memory and payload overview text. */
    std::string memoryText;
    /** Selected long-term memory entities. */
    std::vector<MemoryEntity> entities;
    /** Selected long-term memory relations. */
    std::vector<MemoryRelation> relations;
    /** Selected payload references. */
    std::vector<MemoryPayloadRef> payloadRefs;
    /** Source references used to assemble the context. */
    std::vector<std::string> citations;
    /** Build statistics and implementation metadata. */
    nlohmann::json metadata = nlohmann::json::object();
};

/** Result of BuildContext. */
struct AGENT_MEMORY_API MemoryContextResult
{
    /** True when the context was built successfully. */
    bool succeeded{false};
    /** Context package when succeeded is true. */
    MemoryContextPackage context;
    /** Error information when succeeded is false. */
    MemoryError error;

    /** Returns true when context building succeeded. */
    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
