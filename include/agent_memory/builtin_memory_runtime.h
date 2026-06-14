#pragma once

#include <memory>

#include "agent_memory/runtime.h"

namespace agent_memory {

struct BuiltinMemoryRuntimeImpl;

class BuiltinMemoryRuntime : public MemoryRuntime
{
public:
    explicit BuiltinMemoryRuntime(MemoryConfig config);
    ~BuiltinMemoryRuntime() override;

    MemoryOperationResult AppendEvent(const MemoryEvent& event) override;
    MemoryContextResult BuildContext(const MemoryContextRequest& request) override;
    MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request) override;
    MemoryPayloadReadResult ReadPayload(const std::string& uri) override;
    MemoryConsolidationResult Consolidate(const MemoryConsolidationRequest& request) override;
    MemoryConsolidationResult Consolidate(const MemoryConsolidationRequest& request, ModelClient* model) override;
    MemorySearchResponse SearchMemory(const MemorySearchRequest& request) override;
    MemoryStatsResult GetStats() const override;

private:
    std::unique_ptr<BuiltinMemoryRuntimeImpl> impl_;
};

} // namespace agent_memory
