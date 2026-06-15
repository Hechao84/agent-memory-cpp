#pragma once

#include <functional>
#include <string>
#include <vector>

#include "agent_memory/event.h"
#include "agent_memory/long_term_memory.h"
#include "agent_memory/payload.h"
#include "agent_memory/search.h"
#include "agent_memory/stats.h"

namespace agent_memory {

struct LongTermSummaryRecord
{
    std::string level;
    std::string topic;
    std::string summary;
    float confidence{0.0F};
    std::vector<std::string> sourceRefs;
};

struct LongTermMemorySnapshot
{
    std::vector<LongTermSummaryRecord> summaries;
    std::vector<MemoryEntity> entities;
    std::vector<MemoryRelation> relations;
};

class MemoryStoreTransaction
{
public:
    virtual ~MemoryStoreTransaction() = default;

    virtual bool SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                             const std::string& topic, const std::string& summary, float confidence,
                             const std::vector<std::string>& sourceRefs = {}) = 0;
    virtual bool SaveEntity(const MemoryEntity& entity) = 0;
    virtual bool SaveRelation(const MemoryRelation& relation) = 0;
    virtual bool MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy) = 0;
};

class MemoryStore
{
public:
    virtual ~MemoryStore() = default;

    virtual bool Initialize() = 0;
    virtual bool SaveEvent(const MemoryEvent& event) = 0;
    virtual bool SavePayload(const MemoryPayloadRef& payload) = 0;
    virtual std::vector<MemoryPayloadRef> LoadRecentPayloads(const std::string& agentId, const std::string& sessionId, int limit) const = 0;
    virtual bool SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                             const std::string& topic, const std::string& summary, float confidence,
                             const std::vector<std::string>& sourceRefs = {}) = 0;
    virtual bool SaveEntity(const MemoryEntity& entity) = 0;
    virtual bool SaveRelation(const MemoryRelation& relation) = 0;
    virtual bool MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy) = 0;
    virtual bool RunInTransaction(const std::function<bool(MemoryStoreTransaction& transaction)>& work) = 0;
    virtual std::string LoadConsolidationCursor(const std::string& agentId, const std::string& sessionId) const = 0;
    virtual bool SaveConsolidationCursor(const std::string& agentId, const std::string& sessionId,
                                         const std::string& cursor) = 0;
    virtual std::vector<MemoryEvent> LoadEventsAfterCursor(const std::string& agentId, const std::string& sessionId,
                                                           const std::string& cursor) const = 0;
    virtual std::vector<MemoryEvent> LoadRecentEvents(const std::string& agentId, const std::string& sessionId,
                                                      int limit) const = 0;
    virtual LongTermMemorySnapshot LoadLongTermMemory(const std::string& agentId, int limit,
                                                      const std::string& sessionId = {}) const = 0;

    virtual std::vector<MemorySearchResult> SearchLongTermMemory(const MemorySearchRequest& request) const = 0;
    virtual MemoryStats GetStoreStats() const = 0;
};

} // namespace agent_memory
