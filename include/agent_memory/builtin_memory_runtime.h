#pragma once

#include <memory>
#include <string>

#include "agent_memory/runtime.h"

namespace agent_memory {

struct BuiltinMemoryRuntimeImpl;

/** Status of the runtime-owned built-in model client. */
struct AGENT_MEMORY_API MemoryModelStatus
{
    /** True when MemoryConfig.model.enabled was set. */
    bool configured{false};
    /** True when the configured model client was loaded successfully. */
    bool available{false};
    /** Provider protocol, such as openai or anthropic. */
    std::string formatType;
    /** Configured model name. */
    std::string modelName;
    /** Model loading or validation error, if any. */
    std::string error;

    /** Returns true when the built-in model is available. */
    explicit operator bool() const { return available; }
};

/** Built-in local-first runtime backed by SQLite and the local filesystem. */
class AGENT_MEMORY_API BuiltinMemoryRuntime : public MemoryRuntime
{
public:
    /** Creates a runtime and initializes internal services. Store errors are reported by method results. */
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

    /** Returns status for the runtime-owned built-in model client. */
    MemoryModelStatus GetModelStatus() const;

private:
    std::unique_ptr<BuiltinMemoryRuntimeImpl> impl_;
};

} // namespace agent_memory
