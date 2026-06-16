#pragma once

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

/** Runtime Store statistics. */
struct AGENT_MEMORY_API MemoryStats
{
    /** Number of stored events. */
    int events{0};
    /** Number of stored payload references. */
    int payloads{0};
    /** Number of stored summaries. */
    int summaries{0};
    /** Number of stored entities. */
    int entities{0};
    /** Number of stored relations. */
    int relations{0};
    /** Reserved implementation metadata. */
    nlohmann::json metadata = nlohmann::json::object();
};

/** Result of GetStats. */
struct AGENT_MEMORY_API MemoryStatsResult
{
    /** True when stats were loaded successfully. */
    bool succeeded{false};
    /** Stats payload. */
    MemoryStats stats;
    /** Error information when succeeded is false. */
    MemoryError error;

    /** Returns true when stats loading succeeded. */
    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
