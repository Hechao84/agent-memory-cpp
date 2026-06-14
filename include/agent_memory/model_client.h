#pragma once

#include <string>

#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API ModelInvokeResult
{
    std::string text;
    int httpStatus{0};
    std::string errorCode;
    std::string errorMessage;
    std::string providerError;

    explicit operator bool() const { return errorMessage.empty() && !text.empty(); }
};

class AGENT_MEMORY_API ModelClient
{
public:
    virtual ~ModelClient() = default;
    virtual ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) = 0;
};

} // namespace agent_memory
