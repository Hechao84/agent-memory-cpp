#pragma once

#include <string>
#include <utility>

#include "agent_memory/export.h"

namespace agent_memory {

/** Structured error returned by SDK result objects. */
struct AGENT_MEMORY_API MemoryError
{
    /** Stable machine-readable error code. Empty means no error. */
    std::string code;
    /** Human-readable error summary. */
    std::string message;
    /** Optional lower-level diagnostic details. */
    std::string details;
    /** Whether retrying the same operation may succeed. */
    bool retryable{false};

    /** Returns true when an error is present. */
    bool HasError() const { return !code.empty(); }

    /** Returns true when no error is present. */
    bool Empty() const { return code.empty(); }
};

/** Generic success/error result for operations without a data payload. */
struct AGENT_MEMORY_API MemoryOperationResult
{
    /** True when the operation completed successfully. */
    bool succeeded{false};
    /** Error information when succeeded is false. */
    MemoryError error;

    /** Returns true when the operation succeeded. */
    explicit operator bool() const { return succeeded; }
};

inline MemoryOperationResult MemorySuccess()
{
    return {true, {}};
}

inline MemoryOperationResult MemoryFailure(std::string code, std::string message, std::string details = std::string(), bool retryable = false)
{
    return {false, {std::move(code), std::move(message), std::move(details), retryable}};
}

} // namespace agent_memory
