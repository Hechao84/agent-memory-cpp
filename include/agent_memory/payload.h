#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

/** Reference to an offloaded payload stored outside the event stream. */
struct AGENT_MEMORY_API MemoryPayloadRef
{
    /** Agent namespace for the payload. */
    std::string agentId;
    /** Session namespace for the payload. */
    std::string sessionId;
    /** Payload URI. Current built-in implementation uses file:// URIs. */
    std::string uri;
    /** MIME-like content type supplied by the caller. */
    std::string contentType;
    /** Short generated payload summary. */
    std::string summary;
    /** Tool name associated with the payload. */
    std::string toolName;
    /** Original payload character count. */
    int originalChars{0};
    /** Caller-defined metadata. */
    nlohmann::json metadata = nlohmann::json::object();
    /** Store-assigned creation timestamp. */
    std::string createdAt;
};

/** Request to write or offload a large payload. */
struct AGENT_MEMORY_API MemoryPayloadWriteRequest
{
    /** Agent namespace for the payload. */
    std::string agentId;
    /** Session namespace for the payload. */
    std::string sessionId;
    /** Payload content. */
    std::string content;
    /** MIME-like content type. */
    std::string contentType;
    /** Optional tool call id. */
    std::string toolCallId;
    /** Optional tool name. */
    std::string toolName;
    /** Caller-defined metadata. */
    nlohmann::json metadata = nlohmann::json::object();
};

/** Result of writing or offloading a payload. */
struct AGENT_MEMORY_API MemoryPayloadWriteResult
{
    /** True when the operation completed successfully. */
    bool succeeded{false};
    /** True when content was written to external storage instead of returned inline. */
    bool offloaded{false};
    /** Payload reference when offloaded is true. */
    MemoryPayloadRef payload;
    /** Content that callers can place in the event stream; original content when not offloaded. */
    std::string replacementContent;
    /** Error information when succeeded is false. */
    MemoryError error;

    /** Returns true when the operation succeeded. */
    explicit operator bool() const { return succeeded; }
};

/** Result of reading an offloaded payload. */
struct AGENT_MEMORY_API MemoryPayloadReadResult
{
    /** True when content was read successfully. */
    bool succeeded{false};
    /** Payload content. */
    std::string content;
    /** Error information when succeeded is false. */
    MemoryError error;

    /** Returns true when the read succeeded. */
    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
