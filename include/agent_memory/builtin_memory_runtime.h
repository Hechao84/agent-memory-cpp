#pragma once

#include <memory>
#include <string>

#include "agent_memory/runtime.h"

namespace agent_memory {

struct BuiltinMemoryRuntimeImpl;

struct AGENT_MEMORY_API MemoryModelStatus
{
    bool configured{false};
    bool available{false};
    std::string formatType;
    std::string modelName;
    std::string error;

    explicit operator bool() const { return available; }
};

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
    MemoryModelStatus GetModelStatus() const;

private:
    std::unique_ptr<BuiltinMemoryRuntimeImpl> impl_;
};

} // namespace agent_memory
