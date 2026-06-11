#pragma once

#include <string>
#include <utility>
#include <vector>

#include "agent_memory/export.h"
#include "agent_memory/model_client.h"
#include "agent_memory/types.h"

namespace agent_memory {

class AGENT_MEMORY_API MemoryRuntime
{
public:
    explicit MemoryRuntime(MemoryConfig config) : config_(std::move(config)) {}
    virtual ~MemoryRuntime() = default;

    virtual bool AppendEvent(const MemoryEvent& event) = 0;
    virtual MemoryContextPackage BuildContext(const MemoryContextRequest& request) = 0;
    virtual MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request) = 0;
    virtual std::string ReadPayload(const std::string& ref) = 0;
    virtual bool Consolidate(const MemoryConsolidationRequest& request) = 0;
    virtual bool Consolidate(const MemoryConsolidationRequest& request, MemoryModelClient* model)
    {
        (void)model;
        return Consolidate(request);
    }
    virtual std::vector<MemorySearchResult> SearchMemory(const MemorySearchRequest& request) = 0;
    virtual MemoryStats GetStats() const = 0;

    MemoryConfig GetConfig() const { return config_; }

protected:
    MemoryConfig config_;
};

} // namespace agent_memory
