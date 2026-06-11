#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "agent_memory/runtime.h"

namespace agent_memory {

class LongTermMemoryProcessor;
class MemorySqliteStore;

class BuiltinMemoryRuntime : public MemoryRuntime
{
public:
    explicit BuiltinMemoryRuntime(MemoryConfig config);
    ~BuiltinMemoryRuntime() override;

    bool AppendEvent(const MemoryEvent& event) override;
    MemoryContextPackage BuildContext(const MemoryContextRequest& request) override;
    MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request) override;
    std::string ReadPayload(const std::string& ref) override;
    bool Consolidate(const MemoryConsolidationRequest& request) override;
    bool Consolidate(const MemoryConsolidationRequest& request, MemoryModelClient* model) override;
    std::vector<MemorySearchResult> SearchMemory(const MemorySearchRequest& request) override;
    MemoryStats GetStats() const override;

private:
    std::vector<MemoryEvent> events_;
    std::vector<MemoryPayloadRef> payloads_;
    std::unique_ptr<LongTermMemoryProcessor> longTermProcessor_;
    std::unique_ptr<MemorySqliteStore> sqliteStore_;
    mutable std::mutex mutex_;

    std::string ResolveDataPath() const;
    int AppendLegacyHistory(const MemoryEvent& event);
    int ReadLegacyCursor(const std::string& cursorFile) const;
    std::string BuildPayloadRef(const MemoryPayloadWriteRequest& request) const;
    std::string BuildPayloadSummary(const MemoryPayloadWriteRequest& request) const;
    std::string LoadFile(const std::string& path) const;
    std::string LoadLegacyMemoryText() const;
};

} // namespace agent_memory
