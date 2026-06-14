#pragma once

#include <string>
#include <utility>

#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemoryError
{
    std::string code;
    std::string message;
    std::string details;
    bool retryable{false};

    explicit operator bool() const { return !code.empty(); }
};

struct AGENT_MEMORY_API MemoryOperationResult
{
    bool succeeded{false};
    MemoryError error;

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
