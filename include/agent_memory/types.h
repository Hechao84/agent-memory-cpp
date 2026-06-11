#pragma once

#include <map>
#include <string>
#include <vector>

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

struct AGENT_MEMORY_API MemoryConfig
{
    bool enabled{true};
    std::string mode{"sdk"};
    std::string provider{"builtin"};
    std::string dataPath;
    std::string serverUrl;
    std::string serverApiKey;
    int serverTimeoutSeconds{10};
    int tokenBudget{4096};
    int hotMessages{30};
    int compressAfterTokens{12000};
    int offloadToolResultChars{8000};
    bool enablePayloadOffload{false};
    bool enableShortTermCompression{false};
    bool enableHierarchicalSummary{false};
    bool enableEntityGraph{false};
    std::map<std::string, std::string> extraParams;
};

struct AGENT_MEMORY_API MemoryMessage
{
    std::string role;
    std::string content;
    std::string toolCallId;
    std::string toolName;
    std::string payloadRef;
};

struct AGENT_MEMORY_API MemoryPayloadRef
{
    std::string ref;
    std::string contentType;
    std::string summary;
    std::string toolName;
    int originalChars{0};
    std::map<std::string, std::string> metadata;
    std::string createdAt;
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
    std::map<std::string, std::string> metadata;
    std::string timestamp;
};

struct AGENT_MEMORY_API MemoryContextRequest
{
    std::string agentId;
    std::string sessionId;
    std::string query;
    int tokenBudget{4096};
    std::vector<std::string> include;
    std::map<std::string, std::string> metadata;
};

struct AGENT_MEMORY_API MemoryEntity
{
    std::string id;
    std::string type;
    std::string name;
    std::string summary;
    float confidence{0.0F};
    std::vector<std::string> sourceRefs;
    std::map<std::string, std::string> metadata;
    std::string createdAt;
    std::string updatedAt;
};

struct AGENT_MEMORY_API MemoryRelation
{
    std::string id;
    std::string fromEntity;
    std::string relation;
    std::string toEntity;
    float confidence{0.0F};
    std::vector<std::string> sourceRefs;
    std::map<std::string, std::string> metadata;
    std::string createdAt;
    std::string updatedAt;
};

struct AGENT_MEMORY_API MemoryContextPackage
{
    std::vector<MemoryMessage> messages;
    std::string memoryText;
    std::vector<MemoryEntity> entities;
    std::vector<MemoryPayloadRef> payloadRefs;
    std::vector<std::string> citations;
    std::map<std::string, std::string> metadata;
};

struct AGENT_MEMORY_API MemoryPayloadWriteRequest
{
    std::string agentId;
    std::string sessionId;
    std::string content;
    std::string contentType;
    std::string toolCallId;
    std::string toolName;
    std::map<std::string, std::string> metadata;
};

struct AGENT_MEMORY_API MemoryPayloadWriteResult
{
    bool offloaded{false};
    MemoryPayloadRef payload;
    std::string replacementContent;
};

struct AGENT_MEMORY_API MemoryConsolidationRequest
{
    std::string agentId;
    std::string sessionId;
    int maxEvents{100};
    bool force{false};
    std::map<std::string, std::string> metadata;
};

struct AGENT_MEMORY_API MemorySearchResult
{
    std::string id;
    std::string type;
    std::string content;
    float score{0.0F};
    std::vector<std::string> sourceRefs;
    std::map<std::string, std::string> metadata;
};

struct AGENT_MEMORY_API MemorySearchRequest
{
    std::string agentId;
    std::string sessionId;
    std::string query;
    int limit{10};
    std::vector<std::string> include;
    std::map<std::string, std::string> metadata;
};

struct AGENT_MEMORY_API MemoryStats
{
    int events{0};
    int payloads{0};
    int summaries{0};
    int entities{0};
    int relations{0};
    std::map<std::string, std::string> metadata;
};

} // namespace agent_memory
