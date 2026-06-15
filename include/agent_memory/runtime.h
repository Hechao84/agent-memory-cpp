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

class AGENT_MEMORY_API MemoryRuntime
{
public:
    explicit MemoryRuntime(MemoryConfig config) : config_(std::move(config)) {}
    virtual ~MemoryRuntime() = default;

    virtual MemoryOperationResult AppendEvent(const MemoryEvent& event) = 0;
    virtual MemoryContextResult BuildContext(const MemoryContextRequest& request) = 0;
    virtual MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request) = 0;
    virtual MemoryPayloadReadResult ReadPayload(const std::string& uri) = 0;

    // Uses the runtime configured model when available, then falls back to rule-based extraction.
    virtual MemoryConsolidationResult Consolidate(const MemoryConsolidationRequest& request) = 0;

    // Uses only the explicitly provided model for this call. Passing nullptr explicitly disables model use.
    // The model must remain valid for the duration of the call; shared model instances must be thread-safe.
    virtual MemoryConsolidationResult Consolidate(const MemoryConsolidationRequest& request, ModelClient* model) = 0;
    virtual MemorySearchResponse SearchMemory(const MemorySearchRequest& request) = 0;
    virtual MemoryStatsResult GetStats() const = 0;

    MemoryConfig GetConfig() const { return config_; }

protected:
    MemoryConfig config_;
};

} // namespace agent_memory
