#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent_memory/export.h"

namespace agent_memory {

/** Event kinds that can be appended to short-term memory. */
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

/** Short-term event stored as the source stream for context and consolidation. */
struct AGENT_MEMORY_API MemoryEvent
{
    /** Event kind. Defaults to MESSAGE_APPENDED for ordinary chat messages. */
    MemoryEventType type{MemoryEventType::MESSAGE_APPENDED};
    /** Agent namespace for the event. */
    std::string agentId;
    /** Session namespace for the event. */
    std::string sessionId;
    /** Message role, such as user, assistant, tool, or system. */
    std::string role;
    /** Message or event content. */
    std::string content;
    /** Optional tool call id associated with the event. */
    std::string toolCallId;
    /** Optional tool name associated with the event. */
    std::string toolName;
    /** Optional payload URI reference, usually returned by WritePayload. */
    std::string payloadRef;
    /** Store-assigned cursor used internally for consolidation progress tracking. */
    std::string storeCursor;
    /** Caller-defined metadata persisted with the event. */
    nlohmann::json metadata = nlohmann::json::object();
    /** Event timestamp. Empty input lets the Store assign the current time. */
    std::string timestamp;
};

} // namespace agent_memory
