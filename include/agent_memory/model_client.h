#pragma once

#include <string>

#include "agent_memory/export.h"

namespace agent_memory {

class AGENT_MEMORY_API MemoryModelClient
{
public:
    virtual ~MemoryModelClient() = default;
    virtual std::string InvokeMemoryExtraction(const std::string& prompt) = 0;
};

} // namespace agent_memory
