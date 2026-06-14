#pragma once

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemoryStats
{
    int events{0};
    int payloads{0};
    int summaries{0};
    int entities{0};
    int relations{0};
    nlohmann::json metadata = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemoryStatsResult
{
    bool succeeded{false};
    MemoryStats stats;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
