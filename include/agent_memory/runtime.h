#pragma once

#include <utility>

#include "agent_memory/config.h"
#include "agent_memory/context.h"
#include "agent_memory/error.h"
#include "agent_memory/event.h"
#include "agent_memory/export.h"
#include "agent_memory/model_client.h"
#include "agent_memory/payload.h"
#include "agent_memory/search.h"
#include "agent_memory/stats.h"

namespace agent_memory {

/**
 * Full-feature facade used by SDK callers and server transports.
 *
 * This interface intentionally groups the core agent-memory capabilities behind
 * one runtime boundary. Keeping it as a facade lets SDK, REST, and MCP share the
 * same integration point while the implementation remains free to split storage,
 * payload, context, consolidation, search, and stats into narrower internal
 * services. Prefer adding internal capability interfaces first; split this
 * public facade only when multiple runtime implementations or partial-capability
 * consumers make the dependency cost visible.
 */
class AGENT_MEMORY_API MemoryRuntime
{
public:
    /** Stores a copy of the runtime configuration. */
    explicit MemoryRuntime(MemoryConfig config) : config_(std::move(config)) {}
    virtual ~MemoryRuntime() = default;

    /** Appends a short-term event to the Store. */
    virtual MemoryOperationResult AppendEvent(const MemoryEvent& event) = 0;
    /** Builds a context package from recent events, long-term memory, and payload refs. */
    virtual MemoryContextResult BuildContext(const MemoryContextRequest& request) = 0;
    /** Writes or offloads a payload depending on configuration and payload size. */
    virtual MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request) = 0;
    /** Reads an offloaded payload by URI. */
    virtual MemoryPayloadReadResult ReadPayload(const std::string& uri) = 0;

    /** Uses the runtime configured model when available, then falls back to rule-based extraction. */
    virtual MemoryConsolidationResult Consolidate(const MemoryConsolidationRequest& request) = 0;

    /**
     * Uses only the explicitly provided model for this call.
     * Passing nullptr explicitly disables model use. The model must remain valid for the duration
     * of the call; shared model instances must be thread-safe.
     */
    virtual MemoryConsolidationResult Consolidate(const MemoryConsolidationRequest& request, ModelClient* model) = 0;
    /** Searches long-term memory. */
    virtual MemorySearchResponse SearchMemory(const MemorySearchRequest& request) = 0;
    /** Returns Store statistics. */
    virtual MemoryStatsResult GetStats() const = 0;

    /** Returns the configuration copy held by this runtime. */
    MemoryConfig GetConfig() const { return config_; }

protected:
    MemoryConfig config_;
};

} // namespace agent_memory
