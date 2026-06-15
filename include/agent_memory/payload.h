#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemoryPayloadRef
{
    std::string agentId;
    std::string sessionId;
    std::string uri;
    std::string contentType;
    std::string summary;
    std::string toolName;
    int originalChars{0};
    nlohmann::json metadata = nlohmann::json::object();
    std::string createdAt;
};

struct AGENT_MEMORY_API MemoryPayloadWriteRequest
{
    std::string agentId;
    std::string sessionId;
    std::string content;
    std::string contentType;
    std::string toolCallId;
    std::string toolName;
    nlohmann::json metadata = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemoryPayloadWriteResult
{
    bool succeeded{false};
    bool offloaded{false};
    MemoryPayloadRef payload;
    std::string replacementContent;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

struct AGENT_MEMORY_API MemoryPayloadReadResult
{
    bool succeeded{false};
    std::string content;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
