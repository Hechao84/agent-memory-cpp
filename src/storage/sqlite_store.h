#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "store.h"

struct sqlite3;

namespace agent_memory {

class MemorySqliteStore : public MemoryStore
{
public:
    explicit MemorySqliteStore(std::string dbPath);
    ~MemorySqliteStore() override;

    MemorySqliteStore(const MemorySqliteStore&) = delete;
    MemorySqliteStore& operator=(const MemorySqliteStore&) = delete;
    MemorySqliteStore(MemorySqliteStore&&) = delete;
    MemorySqliteStore& operator=(MemorySqliteStore&&) = delete;

    bool Initialize() override;
    bool SaveEvent(const MemoryEvent& event) override;
    bool SavePayload(const MemoryPayloadRef& payload) override;
    std::vector<MemoryPayloadRef> LoadRecentPayloads(const std::string& agentId, const std::string& sessionId, int limit) const override;
    bool SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                     const std::string& topic, const std::string& summary, float confidence,
                     const std::vector<std::string>& sourceRefs = {}) override;
    bool SaveEntity(const MemoryEntity& entity) override;
    bool SaveRelation(const MemoryRelation& relation) override;
    bool RunInTransaction(const std::function<bool(MemoryStoreTransaction& transaction)>& work) override;
    bool MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy) override;
    std::string LoadConsolidationCursor(const std::string& agentId, const std::string& sessionId) const override;
    bool SaveConsolidationCursor(const std::string& agentId, const std::string& sessionId, const std::string& cursor) override;
    std::vector<MemoryEvent> LoadEventsAfterCursor(const std::string& agentId, const std::string& sessionId,
                                                    const std::string& cursor) const override;
    std::vector<MemoryEvent> LoadRecentEvents(const std::string& agentId, const std::string& sessionId,
                                              int limit) const override;
    LongTermMemorySnapshot LoadLongTermMemory(const std::string& agentId, int limit,
                                               const std::string& sessionId = {}) const override;
    std::vector<MemorySearchResult> SearchLongTermMemory(const MemorySearchRequest& request) const override;
    MemoryStats GetStoreStats() const override;

private:
    friend class SqliteStoreTransaction;

    std::string dbPath_;
    mutable std::recursive_mutex mutex_;
    sqlite3* db_{nullptr};

    bool ExecuteUnlocked(const std::string& sql) const;
    bool ColumnExistsUnlocked(const std::string& tableName, const std::string& columnName) const;
    bool AddColumnIfMissingUnlocked(const std::string& tableName, const std::string& columnName,
                                     const std::string& definition) const;
    bool SaveEventUnlocked(const MemoryEvent& event);
    bool SavePayloadUnlocked(const MemoryPayloadRef& payload);
    bool SaveSummaryUnlocked(const std::string& agentId, const std::string& sessionId, const std::string& level,
                              const std::string& topic, const std::string& summary, float confidence,
                              const std::vector<std::string>& sourceRefs);
    bool SaveEntityUnlocked(const MemoryEntity& entity);
    bool SaveRelationUnlocked(const MemoryRelation& relation);
    bool MarkDuplicateEntitiesObsoleteUnlocked(const MemoryEntity& entity);
    bool MarkEntityObsoleteUnlocked(const std::string& entityId, const std::string& supersededBy);
    bool SaveConsolidationCursorUnlocked(const std::string& agentId, const std::string& sessionId, const std::string& cursor);
    bool IsValidTableName(const std::string& tableName) const;
};

} // namespace agent_memory