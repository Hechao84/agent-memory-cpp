#include "agent_memory/sqlite_store.h"

#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace agent_memory {

MemorySqliteStore::MemorySqliteStore(std::string dbPath)
    : dbPath_(std::move(dbPath))
{
}

MemorySqliteStore::~MemorySqliteStore()
{
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

bool MemorySqliteStore::Initialize()
{
    if (db_ != nullptr) {
        return true;
    }
    if (sqlite3_open(dbPath_.c_str(), &db_) != SQLITE_OK) {
        return false;
    }

    bool ok = Execute("CREATE TABLE IF NOT EXISTS memory_events ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "agent_id TEXT, "
                   "session_id TEXT, "
                   "event_type INTEGER, "
                   "role TEXT, "
                   "content TEXT, "
                   "payload_ref TEXT, "
                   "tool_call_id TEXT, "
                   "tool_name TEXT, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
           Execute("CREATE TABLE IF NOT EXISTS memory_payloads ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "ref TEXT UNIQUE, "
                   "content_type TEXT, "
                   "tool_name TEXT, "
                   "summary TEXT, "
                   "original_chars INTEGER, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
           Execute("CREATE TABLE IF NOT EXISTS memory_summaries ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "agent_id TEXT, "
                   "session_id TEXT, "
                   "level TEXT, "
                   "topic TEXT, "
                   "summary TEXT, "
                   "source_refs_json TEXT, "
                   "confidence REAL, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                   "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
           Execute("CREATE TABLE IF NOT EXISTS memory_entities ("
                   "id TEXT PRIMARY KEY, "
                   "type TEXT, "
                   "name TEXT, "
                   "summary TEXT, "
                   "confidence REAL, "
                   "metadata_json TEXT, "
                   "active INTEGER DEFAULT 1, "
                   "superseded_by TEXT, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                   "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
           Execute("CREATE TABLE IF NOT EXISTS memory_relations ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "from_entity TEXT, "
                   "relation TEXT, "
                   "to_entity TEXT, "
                   "confidence REAL, "
                   "source_refs_json TEXT, "
                   "active INTEGER DEFAULT 1, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                   "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
           Execute("CREATE INDEX IF NOT EXISTS idx_entities_active ON memory_entities(active);") &&
           Execute("CREATE INDEX IF NOT EXISTS idx_relations_active ON memory_relations(active);");
    ok = AddColumnIfMissing("memory_entities", "active", "INTEGER DEFAULT 1") && ok;
    ok = AddColumnIfMissing("memory_entities", "superseded_by", "TEXT") && ok;
    ok = AddColumnIfMissing("memory_relations", "active", "INTEGER DEFAULT 1") && ok;
    return ok;
}

bool MemorySqliteStore::SaveEvent(const MemoryEvent& event)
{
    if (!Initialize()) {
        return false;
    }

    const char* sql = "INSERT INTO memory_events "
                      "(agent_id, session_id, event_type, role, content, payload_ref, tool_call_id, tool_name) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, event.agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, event.sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(event.type));
    sqlite3_bind_text(stmt, 4, event.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, event.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, event.payloadRef.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, event.toolCallId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, event.toolName.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool MemorySqliteStore::SavePayload(const MemoryPayloadRef& payload)
{
    if (!Initialize()) {
        return false;
    }

    const char* sql = "INSERT OR REPLACE INTO memory_payloads "
                      "(ref, content_type, tool_name, summary, original_chars) "
                      "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, payload.ref.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, payload.contentType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, payload.toolName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, payload.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, payload.originalChars);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool MemorySqliteStore::SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                                    const std::string& topic, const std::string& summary, float confidence,
                                    const std::vector<std::string>& sourceRefs)
{
    if (!Initialize()) {
        return false;
    }

    nlohmann::json refsJson = nlohmann::json::array();
    for (const auto& ref : sourceRefs) {
        refsJson.push_back(ref);
    }
    std::string refsStr = refsJson.dump();

    const char* sql = "INSERT INTO memory_summaries "
                      "(agent_id, session_id, level, topic, summary, confidence, source_refs_json) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, level.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, confidence);
    sqlite3_bind_text(stmt, 7, refsStr.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool MemorySqliteStore::SaveEntity(const MemoryEntity& entity)
{
    if (!Initialize()) {
        return false;
    }

    nlohmann::json refsJson = nlohmann::json::array();
    for (const auto& ref : entity.sourceRefs) {
        refsJson.push_back(ref);
    }

    std::string oldSummary;
    {
        const char* checkSql = "SELECT summary, metadata_json FROM memory_entities WHERE id = ?;";
        sqlite3_stmt* checkStmt = nullptr;
        if (sqlite3_prepare_v2(db_, checkSql, -1, &checkStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(checkStmt, 1, entity.id.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(checkStmt) == SQLITE_ROW) {
                const unsigned char* old = sqlite3_column_text(checkStmt, 0);
                if (old != nullptr) {
                    oldSummary = reinterpret_cast<const char*>(old);
                }
            }
            sqlite3_finalize(checkStmt);
        }
    }

    std::string metadataStr = refsJson.dump();
    if (!oldSummary.empty() && oldSummary != entity.summary) {
        nlohmann::json meta;
        if (!refsJson.empty()) {
            meta["sourceRefs"] = refsJson;
        }
        meta["previousSummary"] = oldSummary;
        metadataStr = meta.dump();
    }

    std::string active = "1";
    std::string supersededBy;
    if (entity.metadata.find("status") != entity.metadata.end()) {
        if (entity.metadata.at("status") == "obsolete") {
            active = "0";
        }
    }
    if (entity.metadata.find("superseded_by") != entity.metadata.end()) {
        supersededBy = entity.metadata.at("superseded_by");
    }

    const char* sql = "INSERT OR REPLACE INTO memory_entities "
                      "(id, type, name, summary, confidence, metadata_json, active, superseded_by, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, entity.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entity.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entity.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entity.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, entity.confidence);
    sqlite3_bind_text(stmt, 6, metadataStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, active.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, supersededBy.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok && entity.metadata.find("supersedes") != entity.metadata.end()) {
        MarkEntityObsolete(entity.metadata.at("supersedes"), entity.id);
    }

    return ok;
}

bool MemorySqliteStore::SaveRelation(const MemoryRelation& relation)
{
    if (!Initialize()) {
        return false;
    }

    nlohmann::json refsJson = nlohmann::json::array();
    for (const auto& ref : relation.sourceRefs) {
        refsJson.push_back(ref);
    }
    std::string refsStr = refsJson.dump();

    {
        const char* obsoleteSql = "UPDATE memory_relations SET active = 0, updated_at = CURRENT_TIMESTAMP "
                                  "WHERE from_entity = ? AND relation = ? AND active = 1;";
        sqlite3_stmt* obsoleteStmt = nullptr;
        if (sqlite3_prepare_v2(db_, obsoleteSql, -1, &obsoleteStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(obsoleteStmt, 1, relation.fromEntity.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(obsoleteStmt, 2, relation.relation.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(obsoleteStmt);
            sqlite3_finalize(obsoleteStmt);
        }
    }

    const char* sql = "INSERT INTO memory_relations "
                      "(from_entity, relation, to_entity, confidence, source_refs_json, active) "
                      "VALUES (?, ?, ?, ?, ?, 1);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, relation.fromEntity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, relation.relation.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, relation.toEntity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, relation.confidence);
    sqlite3_bind_text(stmt, 5, refsStr.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (ok && relation.relation == "supersedes") {
        MarkEntityObsolete(relation.toEntity, relation.fromEntity);
    }

    return ok;
}

bool MemorySqliteStore::MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy)
{
    if (!Initialize()) {
        return false;
    }

    const char* sql = "UPDATE memory_entities SET active = 0, superseded_by = ?, "
                      "updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, supersededBy.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entityId.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::string MemorySqliteStore::LoadLongTermMemoryText(int limit) const
{
    if (db_ == nullptr) {
        return "";
    }

    std::stringstream text;

    sqlite3_stmt* stmt = nullptr;
    std::string summarySql = "SELECT level, topic, summary, confidence, source_refs_json FROM memory_summaries "
                             "ORDER BY updated_at DESC, id DESC LIMIT " + std::to_string(limit) + ";";
    if (sqlite3_prepare_v2(db_, summarySql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        bool wroteHeader = false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!wroteHeader) {
                text << "## Long-term Summaries\n";
                wroteHeader = true;
            }
            const unsigned char* level = sqlite3_column_text(stmt, 0);
            const unsigned char* topic = sqlite3_column_text(stmt, 1);
            const unsigned char* summary = sqlite3_column_text(stmt, 2);
            double confidence = sqlite3_column_double(stmt, 3);
            const unsigned char* sources = sqlite3_column_text(stmt, 4);
            text << "- [" << (level ? reinterpret_cast<const char*>(level) : "") << "] "
                 << (topic ? reinterpret_cast<const char*>(topic) : "") << ": "
                 << (summary ? reinterpret_cast<const char*>(summary) : "")
                 << " (confidence=" << confidence << ")";
            if (sources != nullptr && std::string(reinterpret_cast<const char*>(sources)) != "[]") {
                text << " sources=" << reinterpret_cast<const char*>(sources);
            }
            text << "\n";
        }
    }
    if (stmt != nullptr) {
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }

    std::string entitySql = "SELECT id, type, name, summary, confidence, metadata_json FROM memory_entities "
                            "WHERE active = 1 ORDER BY updated_at DESC LIMIT " + std::to_string(limit) + ";";
    if (sqlite3_prepare_v2(db_, entitySql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        bool wroteHeader = false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!wroteHeader) {
                if (text.tellp() > 0) text << "\n";
                text << "## Memory Entities\n";
                wroteHeader = true;
            }
            const unsigned char* id = sqlite3_column_text(stmt, 0);
            const unsigned char* type = sqlite3_column_text(stmt, 1);
            const unsigned char* name = sqlite3_column_text(stmt, 2);
            const unsigned char* summary = sqlite3_column_text(stmt, 3);
            double confidence = sqlite3_column_double(stmt, 4);
            const unsigned char* sources = sqlite3_column_text(stmt, 5);
            text << "- " << (id ? reinterpret_cast<const char*>(id) : "")
                 << " (" << (type ? reinterpret_cast<const char*>(type) : "") << ", "
                 << (name ? reinterpret_cast<const char*>(name) : "") << "): "
                 << (summary ? reinterpret_cast<const char*>(summary) : "")
                 << " (confidence=" << confidence << ")";
            if (sources != nullptr && std::string(reinterpret_cast<const char*>(sources)) != "[]") {
                text << " sources=" << reinterpret_cast<const char*>(sources);
            }
            text << "\n";
        }
    }
    if (stmt != nullptr) {
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }

    std::string relationSql = "SELECT from_entity, relation, to_entity, confidence, source_refs_json FROM memory_relations "
                              "WHERE active = 1 ORDER BY updated_at DESC, id DESC LIMIT " + std::to_string(limit) + ";";
    if (sqlite3_prepare_v2(db_, relationSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        bool wroteHeader = false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!wroteHeader) {
                if (text.tellp() > 0) text << "\n";
                text << "## Memory Relations\n";
                wroteHeader = true;
            }
            const unsigned char* from = sqlite3_column_text(stmt, 0);
            const unsigned char* relation = sqlite3_column_text(stmt, 1);
            const unsigned char* to = sqlite3_column_text(stmt, 2);
            double confidence = sqlite3_column_double(stmt, 3);
            const unsigned char* sources = sqlite3_column_text(stmt, 4);
            text << "- " << (from ? reinterpret_cast<const char*>(from) : "") << " "
                 << (relation ? reinterpret_cast<const char*>(relation) : "") << " "
                 << (to ? reinterpret_cast<const char*>(to) : "")
                 << " (confidence=" << confidence << ")";
            if (sources != nullptr && std::string(reinterpret_cast<const char*>(sources)) != "[]") {
                text << " sources=" << reinterpret_cast<const char*>(sources);
            }
            text << "\n";
        }
    }
    if (stmt != nullptr) {
        sqlite3_finalize(stmt);
    }

    return text.str();
}

int MemorySqliteStore::CountRows(const std::string& tableName) const
{
    if (db_ == nullptr) {
        return 0;
    }

    std::string sql = "SELECT COUNT(*) FROM " + tableName + ";";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool MemorySqliteStore::Execute(const std::string& sql) const
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (errMsg != nullptr) {
        sqlite3_free(errMsg);
    }
    return rc == SQLITE_OK;
}

bool MemorySqliteStore::ColumnExists(const std::string& tableName, const std::string& columnName) const
{
    if (db_ == nullptr) {
        return false;
    }
    std::string sql = "PRAGMA table_info(" + tableName + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name != nullptr && columnName == reinterpret_cast<const char*>(name)) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool MemorySqliteStore::AddColumnIfMissing(const std::string& tableName, const std::string& columnName,
                                            const std::string& definition) const
{
    if (ColumnExists(tableName, columnName)) {
        return true;
    }
    std::string sql = "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + definition + ";";
    return Execute(sql);
}

} // namespace agent_memory
