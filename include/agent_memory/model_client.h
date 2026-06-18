#pragma once

#include <string>

#include "agent_memory/export.h"

namespace agent_memory {

/** Result returned by a model invocation. */
struct AGENT_MEMORY_API ModelInvokeResult
{
    /** Model output text. Consolidation expects JSON text describing memory updates. */
    std::string text;
    /** HTTP status for provider-backed clients; custom clients may leave this as 0. */
    int httpStatus{0};
    /** Provider/client error code such as invalid_config, http_error, or parse_error. */
    std::string errorCode;
    /** Human-readable error message. Empty means no model-side error. */
    std::string errorMessage;
    /** Optional raw provider error body for diagnostics. */
    std::string providerError;

    /** Returns true when text is non-empty and no error message is present. */
    explicit operator bool() const { return errorMessage.empty() && !text.empty(); }
};

/** Host-implemented or built-in model client used by consolidation. */
class AGENT_MEMORY_API MemoryModelClient
{
public:
    virtual ~MemoryModelClient() = default;

    /** Generates a long-term-memory update JSON string from the supplied prompt. */
    virtual ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) = 0;
};

} // namespace agent_memory
