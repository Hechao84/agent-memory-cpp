#pragma once

#include <string>

#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemoryConfig
{
    std::string dataPath;
    int tokenBudget{4096};
    int offloadThresholdChars{8000};
    bool enablePayloadOffload{false};
};

} // namespace agent_memory
