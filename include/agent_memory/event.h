#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "agent_memory/export.h"

namespace agent_memory {

/** Event kinds that can be appended to short-term memory. */
enum class MemoryEventType
{
    SESSION_STARTED,       ///< 会话正式开始
    SESSION_ENDED,         ///< 会话结束
    MESSAGE_APPENDED,      ///< 普通文本消息追加（user/assistant/system/tool 消息）
    TOOL_CALL_STARTED,      ///< 工具调用开始
    TOOL_CALL_FINISHED,    ///< 工具调用完成，结果已写入
    PAYLOAD_OFFLOADED,     ///< 大内容已卸载到外部文件，仅保留引用
    CONSOLIDATION_REQUESTED, ///< 手动请求触发长期记忆 consolidation
    CONSOLIDATION_COMPLETED, ///< consolidation 完成
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
