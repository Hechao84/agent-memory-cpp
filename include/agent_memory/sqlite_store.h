#pragma once

#include <string>
#include <vector>

#include "agent_memory/types.h"

struct sqlite3;

namespace agent_memory {

class MemorySqliteStore
{
public:
    explicit MemorySqliteStore(std::string dbPath);
    ~MemorySqliteStore();

    bool Initialize();
    bool SaveEvent(const MemoryEvent& event);
    bool SavePayload(const MemoryPayloadRef& payload);
    bool SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                     const std::string& topic, const std::string& summary, float confidence,
                     const std::vector<std::string>& sourceRefs = {});
    bool SaveEntity(const MemoryEntity& entity);
    bool SaveRelation(const MemoryRelation& relation);
    bool MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy);
    std::string LoadLongTermMemoryText(int limit) const;
    int CountRows(const std::string& tableName) const;

private:
    std::string dbPath_;
    sqlite3* db_{nullptr};

    bool Execute(const std::string& sql) const;
    bool ColumnExists(const std::string& tableName, const std::string& columnName) const;
    bool AddColumnIfMissing(const std::string& tableName, const std::string& columnName, const std::string& definition) const;
};

} // namespace agent_memory
