#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent_memory/export.h"

namespace agent_memory {

enum class MemoryEventType
{
    SESSION_STARTED,
    SESSION_ENDED,
    MESSAGE_APPENDED,
    TOOL_CALL_STARTED,
    TOOL_CALL_FINISHED,
    PAYLOAD_OFFLOADED,
    CONSOLIDATION_REQUESTED,
    CONSOLIDATION_COMPLETED,
};

struct AGENT_MEMORY_API MemoryEvent
{
    MemoryEventType type{MemoryEventType::MESSAGE_APPENDED};
    std::string agentId;
    std::string sessionId;
    std::string role;
    std::string content;
    std::string toolCallId;
    std::string toolName;
    std::string payloadRef;
    std::string storeCursor;
    nlohmann::json metadata = nlohmann::json::object();
    std::string timestamp;
};

} // namespace agent_memory
