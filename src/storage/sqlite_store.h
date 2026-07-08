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

    MemoryOperationResult Initialize() override;
    MemoryOperationResult SaveEvent(const MemoryEvent& event) override;
    MemoryOperationResult SavePayload(const MemoryPayloadRef& payload) override;
    MemoryPayloadRefsResult LoadRecentPayloads(const std::string& agentId, const std::string& sessionId, int limit) const override;
    MemoryOperationResult SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                                      const std::string& topic, const std::string& summary, float confidence,
                                      const std::vector<std::string>& sourceRefs = {}) override;
    MemoryOperationResult SaveEntity(const MemoryEntity& entity) override;
    MemoryOperationResult SaveRelation(const MemoryRelation& relation) override;
    MemoryOperationResult RunInTransaction(const std::function<MemoryOperationResult(MemoryStoreTransaction& transaction)>& work) override;
    MemoryOperationResult MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy) override;
    ConsolidationCursorResult LoadConsolidationCursor(const std::string& agentId, const std::string& sessionId) const override;
    MemoryOperationResult SaveConsolidationCursor(const std::string& agentId, const std::string& sessionId, const std::string& cursor) override;
    MemoryEventsResult LoadEventsAfterCursor(const std::string& agentId, const std::string& sessionId,
                                             const std::string& cursor,
                                             const std::vector<std::string>& excludedSessionIds = {}) const override;
    MemoryEventsResult LoadRecentEvents(const std::string& agentId, const std::string& sessionId,
                                        int limit) const override;
    LongTermMemorySnapshotResult LoadLongTermMemory(const std::string& agentId, int limit,
                                                    const std::string& sessionId = {}) const override;
    MemorySearchStoreResult SearchLongTermMemory(const MemorySearchRequest& request) const override;
    MemoryStatsResult GetStoreStats() const override;

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