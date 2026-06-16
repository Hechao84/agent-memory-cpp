#include "sqlite_store.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace agent_memory {

namespace {

class SQLiteStatement
{
public:
    SQLiteStatement(sqlite3* db, const std::string& sql)
    {
        if (sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
            if (db != nullptr) {
                std::fprintf(stderr, "[agent_memory] sqlite prepare failed for '%s': %s\n",
                             sql.c_str(), sqlite3_errmsg(db));
            }
            stmt_ = nullptr;
        }
    }

    SQLiteStatement(sqlite3* db, const char* sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            if (db != nullptr) {
                std::fprintf(stderr, "[agent_memory] sqlite prepare failed for '%s': %s\n",
                             sql, sqlite3_errmsg(db));
            }
            stmt_ = nullptr;
        }
    }

    ~SQLiteStatement()
    {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    SQLiteStatement(const SQLiteStatement&) = delete;
    SQLiteStatement& operator=(const SQLiteStatement&) = delete;
    SQLiteStatement(SQLiteStatement&& other) noexcept : stmt_(other.stmt_)
    {
        other.stmt_ = nullptr;
    }
    SQLiteStatement& operator=(SQLiteStatement&& other) noexcept
    {
        if (this != &other) {
            if (stmt_ != nullptr) {
                sqlite3_finalize(stmt_);
            }
            stmt_ = other.stmt_;
            other.stmt_ = nullptr;
        }
        return *this;
    }

    sqlite3_stmt* get() const { return stmt_; }
    explicit operator bool() const { return stmt_ != nullptr; }

private:
    sqlite3_stmt* stmt_{nullptr};
};

std::string ColumnText(sqlite3_stmt* stmt, int col)
{
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

std::string SerializeSourceRefs(const std::vector<std::string>& refs)
{
    return nlohmann::json(refs).dump();
}

nlohmann::json ParseJsonSafely(const std::string& text)
{
    auto json = nlohmann::json::parse(text, nullptr, false);
    return json.is_object() ? json : nlohmann::json::object();
}

nlohmann::json ParseJsonColumn(sqlite3_stmt* stmt, int col)
{
    const unsigned char* text = sqlite3_column_text(stmt, col);
    if (text == nullptr) {
        return nlohmann::json::object();
    }
    return ParseJsonSafely(reinterpret_cast<const char*>(text));
}

std::vector<std::string> ExtractSourceRefsFromJson(const nlohmann::json& json)
{
    std::vector<std::string> refs;
    const nlohmann::json& source = json.is_object() && json.contains("sourceRefs") ? json["sourceRefs"] : json;
    if (!source.is_array()) {
        return refs;
    }
    for (const auto& item : source) {
        if (item.is_string()) {
            refs.push_back(item.get<std::string>());
        }
    }
    return refs;
}

std::vector<std::string> ParseSourceRefsColumn(sqlite3_stmt* stmt, int col)
{
    std::string text = ColumnText(stmt, col);
    if (text.empty()) {
        return {};
    }
    auto json = nlohmann::json::parse(text, nullptr, false);
    return ExtractSourceRefsFromJson(json);
}

MemoryEvent ReadEventRow(sqlite3_stmt* stmt)
{
    MemoryEvent event;
    event.storeCursor = std::to_string(sqlite3_column_int(stmt, 0));
    event.agentId = ColumnText(stmt, 1);
    event.sessionId = ColumnText(stmt, 2);
    event.type = static_cast<MemoryEventType>(sqlite3_column_int(stmt, 3));
    event.role = ColumnText(stmt, 4);
    event.content = ColumnText(stmt, 5);
    event.payloadRef = ColumnText(stmt, 6);
    event.toolCallId = ColumnText(stmt, 7);
    event.toolName = ColumnText(stmt, 8);
    event.metadata = ParseJsonColumn(stmt, 9);
    return event;
}

constexpr std::array<std::string_view, 6> VALID_TABLE_NAMES = {
    "memory_events",
    "memory_payloads",
    "memory_summaries",
    "memory_entities",
    "memory_relations",
    "memory_consolidation_cursors",
};

std::string SqliteErrorMessage(sqlite3* db)
{
    if (db == nullptr) {
        return "sqlite database is not initialized";
    }
    std::string message = sqlite3_errmsg(db);
    return message.empty() ? "sqlite operation failed" : message;
}

MemoryOperationResult SqliteFailure(sqlite3* db, const std::string& context)
{
    std::string details = SqliteErrorMessage(db);
    if (!details.empty() && details != "not an error") {
        std::fprintf(stderr, "[agent_memory] sqlite error in %s: %s\n", context.c_str(), details.c_str());
    }
    return MemoryFailure("sqlite_error", "sqlite operation failed", context + ": " + details, true);
}

void LogSqliteError(sqlite3* db, const std::string& context)
{
    SqliteFailure(db, context);
}

void LogSqliteExecError(char* errMsg, const std::string& context)
{
    if (errMsg != nullptr) {
        std::fprintf(stderr, "[agent_memory] sqlite exec error in %s: %s\n", context.c_str(), errMsg);
        sqlite3_free(errMsg);
    }
}

int CursorToInt(const std::string& cursor)
{
    if (cursor.empty()) {
        return 0;
    }
    try {
        return std::stoi(cursor);
    } catch (...) {
        return 0;
    }
}

std::string EscapeLikePattern(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '%' || ch == '_') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

struct SearchScoreConfig
{
    double summaryWeight{1.0};
    double entityWeight{0.95};
    double relationWeight{0.90};
    double summaryFallbackScore{0.30};
    double entityFallbackScore{0.25};
    double relationFallbackScore{0.20};
};

const SearchScoreConfig& SearchScoring()
{
    static const SearchScoreConfig config;
    return config;
}

double TypeWeight(const std::string& type)
{
    const auto& config = SearchScoring();
    if (type == "summary") {
        return config.summaryWeight;
    }
    if (type == "entity") {
        return config.entityWeight;
    }
    return config.relationWeight;
}

double FallbackScore(const std::string& type)
{
    const auto& config = SearchScoring();
    if (type == "summary") {
        return config.summaryFallbackScore;
    }
    if (type == "entity") {
        return config.entityFallbackScore;
    }
    return config.relationFallbackScore;
}

void ApplyFtsScore(MemorySearchResult& result, double bm25Rank)
{
    double rawRank = std::max(0.0, -bm25Rank);
    double normalized = rawRank > 0.0 ? rawRank / (rawRank + 1.0) : 0.0;
    double weight = TypeWeight(result.type);
    result.score = static_cast<float>(normalized * weight);
    result.metadata["scoreSource"] = "fts_bm25";
    result.metadata["rawRank"] = rawRank;
    result.metadata["typeWeight"] = weight;
}

void ApplyFallbackScore(MemorySearchResult& result)
{
    result.score = static_cast<float>(FallbackScore(result.type));
    result.metadata["scoreSource"] = "like_fallback";
}

void SortAndLimitSearchResults(std::vector<MemorySearchResult>& results, int limit)
{
    std::sort(results.begin(), results.end(), [](const MemorySearchResult& lhs, const MemorySearchResult& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.id < rhs.id;
    });
    if (limit > 0 && static_cast<int>(results.size()) > limit) {
        results.resize(static_cast<size_t>(limit));
    }
}

std::string Fts5EscapeQuery(const std::string& query)
{
    std::string escaped;
    for (char c : query) {
        switch (c) {
        case '"':
        case ':':
        case '^':
        case '!':
        case '+':
        case '-':
        case '~':
        case '@':
        case '#':
        case '$':
        case '%':
        case '&':
        case '|':
        case '(':
        case ')':
        case '=':
        case ';':
        case '<':
        case '>':
        case ',':
        case '/':
        case '\\':
        case '[':
        case ']':
        case '?':
            continue;
        case '*':
            if (escaped.empty()) {
                continue;
            }
            escaped += c;
            break;
        default:
            escaped += c;
            break;
        }
    }
    if (escaped.empty()) {
        for (char c : query) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ' ' || c == '*') {
                escaped += c;
            }
        }
    }
    return escaped;
}

} // namespace

class SqliteStoreTransaction : public MemoryStoreTransaction
{
public:
    explicit SqliteStoreTransaction(MemorySqliteStore& store) : store_(store) {}

    MemoryOperationResult SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                                      const std::string& topic, const std::string& summary, float confidence,
                                      const std::vector<std::string>& sourceRefs = {}) override
    {
        return store_.SaveSummaryUnlocked(agentId, sessionId, level, topic, summary, confidence, sourceRefs) ? MemorySuccess()
                                                                                                            : SqliteFailure(store_.db_, "Transaction::SaveSummary");
    }

    MemoryOperationResult SaveEntity(const MemoryEntity& entity) override
    {
        return store_.SaveEntityUnlocked(entity) ? MemorySuccess() : SqliteFailure(store_.db_, "Transaction::SaveEntity");
    }

    MemoryOperationResult SaveRelation(const MemoryRelation& relation) override
    {
        return store_.SaveRelationUnlocked(relation) ? MemorySuccess() : SqliteFailure(store_.db_, "Transaction::SaveRelation");
    }

    MemoryOperationResult MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy) override
    {
        return store_.MarkEntityObsoleteUnlocked(entityId, supersededBy) ? MemorySuccess()
                                                                         : SqliteFailure(store_.db_, "Transaction::MarkEntityObsolete");
    }

private:
    MemorySqliteStore& store_;
};

MemorySqliteStore::MemorySqliteStore(std::string dbPath)
    : dbPath_(std::move(dbPath))
{
}

MemorySqliteStore::~MemorySqliteStore()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ != nullptr) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

MemoryOperationResult MemorySqliteStore::Initialize()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ != nullptr) {
        return MemorySuccess();
    }
    if (sqlite3_open(dbPath_.c_str(), &db_) != SQLITE_OK) {
        MemoryOperationResult result = SqliteFailure(db_, "Initialize::open");
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
        db_ = nullptr;
        return result;
    }

    sqlite3_busy_timeout(db_, 5000);

    bool ok = ExecuteUnlocked("PRAGMA journal_mode=WAL;") &&
              ExecuteUnlocked("PRAGMA synchronous=NORMAL;") &&
              ExecuteUnlocked("PRAGMA cache_size=-8000;") &&
              ExecuteUnlocked("CREATE TABLE IF NOT EXISTS memory_events ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "agent_id TEXT, "
                   "session_id TEXT, "
                   "event_type INTEGER, "
                   "role TEXT, "
                   "content TEXT, "
                   "payload_ref TEXT, "
                   "tool_call_id TEXT, "
                   "tool_name TEXT, "
                   "metadata_json TEXT, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
           ExecuteUnlocked("CREATE TABLE IF NOT EXISTS memory_payloads ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "agent_id TEXT, "
                   "session_id TEXT, "
                   "ref TEXT UNIQUE, "
                   "content_type TEXT, "
                   "tool_name TEXT, "
                   "summary TEXT, "
                   "original_chars INTEGER, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
           ExecuteUnlocked("CREATE TABLE IF NOT EXISTS memory_summaries ("
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
           ExecuteUnlocked("CREATE TABLE IF NOT EXISTS memory_entities ("
                   "id TEXT PRIMARY KEY, "
                   "agent_id TEXT, "
                   "type TEXT, "
                   "name TEXT, "
                   "summary TEXT, "
                   "confidence REAL, "
                   "source_refs_json TEXT, "
                   "metadata_json TEXT, "
                   "active INTEGER DEFAULT 1, "
                   "superseded_by TEXT, "
                   "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                   "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                   ");") &&
            ExecuteUnlocked("CREATE TABLE IF NOT EXISTS memory_relations ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "agent_id TEXT, "
                    "from_entity TEXT, "
                    "relation TEXT, "
                    "to_entity TEXT, "
                    "confidence REAL, "
                    "source_refs_json TEXT, "
                    "metadata_json TEXT, "
                    "active INTEGER DEFAULT 1, "
                    "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                    "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                    ");") &&
            ExecuteUnlocked("CREATE TABLE IF NOT EXISTS memory_consolidation_cursors ("
                    "agent_id TEXT NOT NULL, "
                    "session_id TEXT NOT NULL, "
                    "cursor INTEGER NOT NULL DEFAULT 0, "
                    "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                    "PRIMARY KEY(agent_id, session_id)"
                    ");") &&
            ExecuteUnlocked("CREATE INDEX IF NOT EXISTS idx_events_agent_session_id ON memory_events(agent_id, session_id, id);") &&
            ExecuteUnlocked("CREATE INDEX IF NOT EXISTS idx_payloads_agent_session_created ON memory_payloads(agent_id, session_id, created_at, id);") &&
            ExecuteUnlocked("CREATE INDEX IF NOT EXISTS idx_summaries_agent_session_updated ON memory_summaries(agent_id, session_id, updated_at, id);") &&
            ExecuteUnlocked("CREATE INDEX IF NOT EXISTS idx_entities_agent_active ON memory_entities(agent_id, active);") &&
            ExecuteUnlocked("CREATE INDEX IF NOT EXISTS idx_relations_agent_active ON memory_relations(agent_id, active);") &&
           ExecuteUnlocked("CREATE INDEX IF NOT EXISTS idx_relations_active ON memory_relations(active);");

    ok = AddColumnIfMissingUnlocked("memory_entities", "agent_id", "TEXT") && ok;
    ok = AddColumnIfMissingUnlocked("memory_entities", "active", "INTEGER DEFAULT 1") && ok;
    ok = AddColumnIfMissingUnlocked("memory_entities", "superseded_by", "TEXT") && ok;
    ok = AddColumnIfMissingUnlocked("memory_entities", "source_refs_json", "TEXT") && ok;
    ok = AddColumnIfMissingUnlocked("memory_relations", "agent_id", "TEXT") && ok;
    ok = AddColumnIfMissingUnlocked("memory_relations", "active", "INTEGER DEFAULT 1") && ok;
    ok = AddColumnIfMissingUnlocked("memory_relations", "source_refs_json", "TEXT") && ok;
    ok = AddColumnIfMissingUnlocked("memory_relations", "metadata_json", "TEXT") && ok;

    ok = ExecuteUnlocked("CREATE VIRTUAL TABLE IF NOT EXISTS fts_memory_summaries USING fts5("
                  "level, topic, summary, "
                  "content=memory_summaries, content_rowid=id"
                  ");") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_sum_ai AFTER INSERT ON memory_summaries BEGIN "
                  "INSERT INTO fts_memory_summaries(rowid, level, topic, summary) "
                  "VALUES (new.id, new.level, new.topic, new.summary);"
                  "END;") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_sum_ad AFTER DELETE ON memory_summaries BEGIN "
                  "INSERT INTO fts_memory_summaries(fts_memory_summaries, rowid, level, topic, summary) "
                  "VALUES('delete', old.id, old.level, old.topic, old.summary);"
                  "END;") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_sum_au AFTER UPDATE ON memory_summaries BEGIN "
                  "INSERT INTO fts_memory_summaries(fts_memory_summaries, rowid, level, topic, summary) "
                  "VALUES('delete', old.id, old.level, old.topic, old.summary);"
                  "INSERT INTO fts_memory_summaries(rowid, level, topic, summary) "
                  "VALUES (new.id, new.level, new.topic, new.summary);"
                  "END;") && ok;

    ok = ExecuteUnlocked("CREATE VIRTUAL TABLE IF NOT EXISTS fts_memory_entities USING fts5("
                  "type, name, summary, "
                  "content=memory_entities, content_rowid=rowid"
                  ");") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_ent_ai AFTER INSERT ON memory_entities BEGIN "
                  "INSERT INTO fts_memory_entities(rowid, type, name, summary) "
                  "VALUES (new.rowid, new.type, new.name, new.summary);"
                  "END;") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_ent_ad AFTER DELETE ON memory_entities BEGIN "
                  "INSERT INTO fts_memory_entities(fts_memory_entities, rowid, type, name, summary) "
                  "VALUES('delete', old.rowid, old.type, old.name, old.summary);"
                  "END;") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_ent_au AFTER UPDATE ON memory_entities BEGIN "
                  "INSERT INTO fts_memory_entities(fts_memory_entities, rowid, type, name, summary) "
                  "VALUES('delete', old.rowid, old.type, old.name, old.summary);"
                  "INSERT INTO fts_memory_entities(rowid, type, name, summary) "
                  "VALUES (new.rowid, new.type, new.name, new.summary);"
                  "END;") && ok;

    ok = ExecuteUnlocked("CREATE VIRTUAL TABLE IF NOT EXISTS fts_memory_relations USING fts5("
                  "from_entity, relation, to_entity, "
                  "content=memory_relations, content_rowid=id"
                  ");") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_rel_ai AFTER INSERT ON memory_relations BEGIN "
                  "INSERT INTO fts_memory_relations(rowid, from_entity, relation, to_entity) "
                  "VALUES (new.id, new.from_entity, new.relation, new.to_entity);"
                  "END;") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_rel_ad AFTER DELETE ON memory_relations BEGIN "
                  "INSERT INTO fts_memory_relations(fts_memory_relations, rowid, from_entity, relation, to_entity) "
                  "VALUES('delete', old.id, old.from_entity, old.relation, old.to_entity);"
                  "END;") && ok;
    ok = ExecuteUnlocked("CREATE TRIGGER IF NOT EXISTS fts_mem_rel_au AFTER UPDATE ON memory_relations BEGIN "
                  "INSERT INTO fts_memory_relations(fts_memory_relations, rowid, from_entity, relation, to_entity) "
                  "VALUES('delete', old.id, old.from_entity, old.relation, old.to_entity);"
                  "INSERT INTO fts_memory_relations(rowid, from_entity, relation, to_entity) "
                  "VALUES (new.id, new.from_entity, new.relation, new.to_entity);"
                  "END;") && ok;

    if (!ok) {
        return SqliteFailure(db_, "Initialize::schema");
    }
    return MemorySuccess();
}

MemoryOperationResult MemorySqliteStore::SaveEvent(const MemoryEvent& event)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    return SaveEventUnlocked(event) ? MemorySuccess() : SqliteFailure(db_, "SaveEvent");
}

MemoryOperationResult MemorySqliteStore::SavePayload(const MemoryPayloadRef& payload)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    return SavePayloadUnlocked(payload) ? MemorySuccess() : SqliteFailure(db_, "SavePayload");
}

MemoryPayloadRefsResult MemorySqliteStore::LoadRecentPayloads(const std::string& agentId, const std::string& sessionId, int limit) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryPayloadRefsResult result;
    if (db_ == nullptr) {
        result.error = MemoryFailure("store_unavailable", "sqlite store is not initialized").error;
        return result;
    }

    const char* sql = "SELECT agent_id, session_id, ref, content_type, tool_name, summary, original_chars, created_at "
                      "FROM memory_payloads WHERE agent_id = ? AND (? = '' OR session_id = ?) "
                      "ORDER BY created_at DESC, id DESC LIMIT ?;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        result.error = SqliteFailure(db_, "LoadRecentPayloads::prepare").error;
        return result;
    }

    sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 4, limit > 0 ? limit : 20);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        MemoryPayloadRef payload;
        payload.agentId = ColumnText(stmt.get(), 0);
        payload.sessionId = ColumnText(stmt.get(), 1);
        payload.uri = ColumnText(stmt.get(), 2);
        payload.contentType = ColumnText(stmt.get(), 3);
        payload.toolName = ColumnText(stmt.get(), 4);
        payload.summary = ColumnText(stmt.get(), 5);
        payload.originalChars = sqlite3_column_int(stmt.get(), 6);
        payload.createdAt = ColumnText(stmt.get(), 7);
        result.payloads.push_back(std::move(payload));
    }
    if (rc != SQLITE_DONE) {
        result.error = SqliteFailure(db_, "LoadRecentPayloads::step").error;
        return result;
    }
    result.succeeded = true;
    return result;
}

MemoryOperationResult MemorySqliteStore::SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                                                       const std::string& topic, const std::string& summary, float confidence,
                                                       const std::vector<std::string>& sourceRefs)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    return SaveSummaryUnlocked(agentId, sessionId, level, topic, summary, confidence, sourceRefs) ? MemorySuccess()
                                                                                                  : SqliteFailure(db_, "SaveSummary");
}

MemoryOperationResult MemorySqliteStore::SaveEntity(const MemoryEntity& entity)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    return SaveEntityUnlocked(entity) ? MemorySuccess() : SqliteFailure(db_, "SaveEntity");
}

MemoryOperationResult MemorySqliteStore::SaveRelation(const MemoryRelation& relation)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    return SaveRelationUnlocked(relation) ? MemorySuccess() : SqliteFailure(db_, "SaveRelation");
}

MemoryOperationResult MemorySqliteStore::RunInTransaction(const std::function<MemoryOperationResult(MemoryStoreTransaction& transaction)>& work)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    if (!ExecuteUnlocked("BEGIN IMMEDIATE TRANSACTION;")) {
        return SqliteFailure(db_, "RunInTransaction::begin");
    }
    SqliteStoreTransaction transaction(*this);
    MemoryOperationResult result = work(transaction);
    if (result) {
        if (!ExecuteUnlocked("COMMIT;")) {
            auto commitResult = SqliteFailure(db_, "RunInTransaction::commit");
            ExecuteUnlocked("ROLLBACK;");
            return commitResult;
        }
        return MemorySuccess();
    }
    ExecuteUnlocked("ROLLBACK;");
    return result.error.HasError() ? result : MemoryFailure("transaction_failed", "transaction work failed");
}

MemoryOperationResult MemorySqliteStore::MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    return MarkEntityObsoleteUnlocked(entityId, supersededBy) ? MemorySuccess() : SqliteFailure(db_, "MarkEntityObsolete");
}

ConsolidationCursorResult MemorySqliteStore::LoadConsolidationCursor(const std::string& agentId, const std::string& sessionId) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ConsolidationCursorResult result;
    if (db_ == nullptr) {
        result.error = MemoryFailure("store_unavailable", "sqlite store is not initialized").error;
        return result;
    }

    const char* sql = "SELECT cursor FROM memory_consolidation_cursors WHERE agent_id = ? AND session_id = ?;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        result.error = SqliteFailure(db_, "LoadConsolidationCursor::prepare").error;
        return result;
    }
    sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        result.cursor = std::to_string(sqlite3_column_int(stmt.get(), 0));
    } else if (rc != SQLITE_DONE) {
        result.error = SqliteFailure(db_, "LoadConsolidationCursor::step").error;
        return result;
    }
    result.succeeded = true;
    return result;
}

MemoryOperationResult MemorySqliteStore::SaveConsolidationCursor(const std::string& agentId, const std::string& sessionId, const std::string& cursor)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_ == nullptr) {
        return MemoryFailure("store_unavailable", "sqlite store is not initialized");
    }
    return SaveConsolidationCursorUnlocked(agentId, sessionId, cursor) ? MemorySuccess()
                                                                       : SqliteFailure(db_, "SaveConsolidationCursor");
}

MemoryEventsResult MemorySqliteStore::LoadEventsAfterCursor(const std::string& agentId,
                                                             const std::string& sessionId, const std::string& cursor) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryEventsResult result;
    if (db_ == nullptr) {
        result.error = MemoryFailure("store_unavailable", "sqlite store is not initialized").error;
        return result;
    }

    int cursorValue = CursorToInt(cursor);
    const char* sqlWithSession = "SELECT id, agent_id, session_id, event_type, role, content, payload_ref, tool_call_id, tool_name, metadata_json "
                                 "FROM memory_events WHERE id > ? AND agent_id = ? AND session_id = ? "
                                 "ORDER BY id ASC;";
    const char* sqlWithoutSession = "SELECT id, agent_id, session_id, event_type, role, content, payload_ref, tool_call_id, tool_name, metadata_json "
                                    "FROM memory_events WHERE id > ? AND agent_id = ? "
                                    "ORDER BY id ASC;";
    SQLiteStatement stmt(db_, sessionId.empty() ? sqlWithoutSession : sqlWithSession);
    if (!stmt) {
        result.error = SqliteFailure(db_, "LoadEventsAfterCursor::prepare").error;
        return result;
    }
    sqlite3_bind_int(stmt.get(), 1, cursorValue);
    sqlite3_bind_text(stmt.get(), 2, agentId.c_str(), -1, SQLITE_TRANSIENT);
    if (!sessionId.empty()) {
        sqlite3_bind_text(stmt.get(), 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    }
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        result.events.push_back(ReadEventRow(stmt.get()));
    }
    if (rc != SQLITE_DONE) {
        result.error = SqliteFailure(db_, "LoadEventsAfterCursor::step").error;
        return result;
    }
    result.succeeded = true;
    return result;
}


MemoryEventsResult MemorySqliteStore::LoadRecentEvents(const std::string& agentId, const std::string& sessionId,
                                                         int limit) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryEventsResult result;
    if (db_ == nullptr) {
        result.error = MemoryFailure("store_unavailable", "sqlite store is not initialized").error;
        return result;
    }

    int effectiveLimit = limit > 0 ? limit : 20;
    const char* sql = "SELECT id, agent_id, session_id, event_type, role, content, payload_ref, tool_call_id, tool_name, metadata_json "
                      "FROM memory_events WHERE agent_id = ? AND (? = '' OR session_id = ?) "
                      "ORDER BY id DESC LIMIT ?;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        result.error = SqliteFailure(db_, "LoadRecentEvents::prepare").error;
        return result;
    }
    sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 4, effectiveLimit);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        result.events.push_back(ReadEventRow(stmt.get()));
    }
    if (rc != SQLITE_DONE) {
        result.error = SqliteFailure(db_, "LoadRecentEvents::step").error;
        return result;
    }
    std::reverse(result.events.begin(), result.events.end());
    result.succeeded = true;
    return result;
}

LongTermMemorySnapshotResult MemorySqliteStore::LoadLongTermMemory(const std::string& agentId, int limit,
                                                                    const std::string& sessionId) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    LongTermMemorySnapshotResult result;
    auto& snapshot = result.snapshot;
    if (db_ == nullptr) {
        result.error = MemoryFailure("store_unavailable", "sqlite store is not initialized").error;
        return result;
    }

    int effectiveLimit = limit > 0 ? limit : 100;

    {
        const char* sql = "SELECT level, topic, summary, confidence, source_refs_json "
                          "FROM memory_summaries "
                          "WHERE agent_id = ? AND (? = '' OR session_id = ?) "
                          "ORDER BY updated_at DESC, id DESC LIMIT ?;";
        SQLiteStatement stmt(db_, sql);
        if (!stmt) {
            result.error = SqliteFailure(db_, "LoadLongTermMemory::summaries_prepare").error;
            return result;
        }
        sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 3, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.get(), 4, effectiveLimit);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            LongTermSummaryRecord record;
            record.level = ColumnText(stmt.get(), 0);
            record.topic = ColumnText(stmt.get(), 1);
            record.summary = ColumnText(stmt.get(), 2);
            record.confidence = static_cast<float>(sqlite3_column_double(stmt.get(), 3));
            record.sourceRefs = ParseSourceRefsColumn(stmt.get(), 4);
            snapshot.summaries.push_back(std::move(record));
        }
        if (rc != SQLITE_DONE) {
            result.error = SqliteFailure(db_, "LoadLongTermMemory::summaries_step").error;
            return result;
        }
    }

    {
        const char* sql = "SELECT id, agent_id, type, name, summary, confidence, source_refs_json, metadata_json FROM memory_entities "
                          "WHERE active = 1 AND agent_id = ? "
                          "ORDER BY updated_at DESC LIMIT ?;";
        SQLiteStatement stmt(db_, sql);
        if (!stmt) {
            result.error = SqliteFailure(db_, "LoadLongTermMemory::entities_prepare").error;
            return result;
        }
        sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.get(), 2, effectiveLimit);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            MemoryEntity entity;
            entity.id = ColumnText(stmt.get(), 0);
            entity.agentId = ColumnText(stmt.get(), 1);
            entity.entityType = ColumnText(stmt.get(), 2);
            entity.name = ColumnText(stmt.get(), 3);
            entity.summary = ColumnText(stmt.get(), 4);
            entity.confidence = static_cast<float>(sqlite3_column_double(stmt.get(), 5));
            entity.sourceRefs = ParseSourceRefsColumn(stmt.get(), 6);
            entity.metadata = ParseJsonColumn(stmt.get(), 7);
            snapshot.entities.push_back(std::move(entity));
        }
        if (rc != SQLITE_DONE) {
            result.error = SqliteFailure(db_, "LoadLongTermMemory::entities_step").error;
            return result;
        }
    }

    {
        const char* sql = "SELECT id, agent_id, from_entity, relation, to_entity, confidence, source_refs_json, metadata_json "
                          "FROM memory_relations "
                          "WHERE active = 1 AND agent_id = ? "
                          "ORDER BY updated_at DESC, id DESC LIMIT ?;";
        SQLiteStatement stmt(db_, sql);
        if (!stmt) {
            result.error = SqliteFailure(db_, "LoadLongTermMemory::relations_prepare").error;
            return result;
        }
        sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.get(), 2, effectiveLimit);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            MemoryRelation relation;
            relation.id = std::to_string(sqlite3_column_int(stmt.get(), 0));
            relation.agentId = ColumnText(stmt.get(), 1);
            relation.fromEntityId = ColumnText(stmt.get(), 2);
            relation.relationType = ColumnText(stmt.get(), 3);
            relation.toEntityId = ColumnText(stmt.get(), 4);
            relation.confidence = static_cast<float>(sqlite3_column_double(stmt.get(), 5));
            relation.sourceRefs = ParseSourceRefsColumn(stmt.get(), 6);
            relation.metadata = ParseJsonColumn(stmt.get(), 7);
            snapshot.relations.push_back(std::move(relation));
        }
        if (rc != SQLITE_DONE) {
            result.error = SqliteFailure(db_, "LoadLongTermMemory::relations_step").error;
            return result;
        }
    }

    result.succeeded = true;
    return result;
}

MemorySearchStoreResult MemorySqliteStore::SearchLongTermMemory(const MemorySearchRequest& request) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemorySearchStoreResult result;
    auto& results = result.results;
    if (db_ == nullptr) {
        result.error = MemoryFailure("store_unavailable", "sqlite store is not initialized").error;
        return result;
    }
    if (request.query.empty()) {
        result.succeeded = true;
        return result;
    }

    int limit = request.limit > 0 ? request.limit : 10;
    std::string ftsQuery = Fts5EscapeQuery(request.query);
    bool ftsFailed = false;

    {
        const char* sql = "SELECT s.id, s.level, s.topic, s.summary, s.source_refs_json, bm25(fts_memory_summaries) AS rank "
                          "FROM memory_summaries s "
                          "JOIN fts_memory_summaries ON fts_memory_summaries.rowid = s.id "
                          "WHERE s.agent_id = ? AND (? = '' OR s.session_id = ?) "
                          "AND fts_memory_summaries MATCH ? "
                          "ORDER BY rank ASC, s.updated_at DESC, s.id DESC LIMIT ?;";
        SQLiteStatement stmt(db_, sql);
        if (!stmt) {
            result.error = SqliteFailure(db_, "SearchLongTermMemory::fts_summaries_prepare").error;
            return result;
        }
        sqlite3_bind_text(stmt.get(), 1, request.agentId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, request.sessionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 3, request.sessionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 4, ftsQuery.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.get(), 5, limit);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            MemorySearchResult item;
            item.id = "summary:" + std::to_string(sqlite3_column_int(stmt.get(), 0));
            item.type = "summary";
            item.content = ColumnText(stmt.get(), 1) + ": " +
                           ColumnText(stmt.get(), 2) + ": " +
                           ColumnText(stmt.get(), 3);
            item.sourceRefs = ParseSourceRefsColumn(stmt.get(), 4);
            ApplyFtsScore(item, sqlite3_column_double(stmt.get(), 5));
            results.push_back(std::move(item));
        }
        if (rc != SQLITE_DONE) {
            LogSqliteError(db_, "SearchLongTermMemory::fts_summaries_step");
            results.clear();
            ftsFailed = true;
        }
    }

    if (!ftsFailed) {
        const char* sql = "SELECT e.id, e.agent_id, e.type, e.name, e.summary, e.source_refs_json, e.metadata_json, bm25(fts_memory_entities) AS rank "
                          "FROM memory_entities e "
                          "JOIN fts_memory_entities ON fts_memory_entities.rowid = e.rowid "
                          "WHERE e.active = 1 AND e.agent_id = ? "
                          "AND fts_memory_entities MATCH ? "
                          "ORDER BY rank ASC, e.updated_at DESC LIMIT ?;";
        SQLiteStatement stmt(db_, sql);
        if (!stmt) {
            result.error = SqliteFailure(db_, "SearchLongTermMemory::fts_entities_prepare").error;
            return result;
        }
        sqlite3_bind_text(stmt.get(), 1, request.agentId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, ftsQuery.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.get(), 3, limit);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            MemorySearchResult item;
            item.id = ColumnText(stmt.get(), 0);
            item.type = "entity";
            item.content = ColumnText(stmt.get(), 2) + ": " +
                           ColumnText(stmt.get(), 3) + ": " +
                           ColumnText(stmt.get(), 4);
            item.sourceRefs = ParseSourceRefsColumn(stmt.get(), 5);
            item.metadata = ParseJsonColumn(stmt.get(), 6);
            ApplyFtsScore(item, sqlite3_column_double(stmt.get(), 7));
            results.push_back(std::move(item));
        }
        if (rc != SQLITE_DONE) {
            LogSqliteError(db_, "SearchLongTermMemory::fts_entities_step");
            results.clear();
            ftsFailed = true;
        }
    }

    if (!ftsFailed) {
        const char* sql = "SELECT r.id, r.from_entity, r.relation, r.to_entity, r.source_refs_json, bm25(fts_memory_relations) AS rank "
                          "FROM memory_relations r "
                          "JOIN fts_memory_relations ON fts_memory_relations.rowid = r.id "
                          "WHERE r.active = 1 AND r.agent_id = ? "
                          "AND fts_memory_relations MATCH ? "
                          "ORDER BY rank ASC, r.updated_at DESC, r.id DESC LIMIT ?;";
        SQLiteStatement stmt(db_, sql);
        if (!stmt) {
            result.error = SqliteFailure(db_, "SearchLongTermMemory::fts_relations_prepare").error;
            return result;
        }
        sqlite3_bind_text(stmt.get(), 1, request.agentId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 2, ftsQuery.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.get(), 3, limit);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            MemorySearchResult item;
            item.id = "relation:" + std::to_string(sqlite3_column_int(stmt.get(), 0));
            item.type = "relation";
            item.content = ColumnText(stmt.get(), 1) + " " +
                           ColumnText(stmt.get(), 2) + " " +
                           ColumnText(stmt.get(), 3);
            item.sourceRefs = ParseSourceRefsColumn(stmt.get(), 4);
            ApplyFtsScore(item, sqlite3_column_double(stmt.get(), 5));
            results.push_back(std::move(item));
        }
        if (rc != SQLITE_DONE) {
            LogSqliteError(db_, "SearchLongTermMemory::fts_relations_step");
            results.clear();
            ftsFailed = true;
        }
    }

    if (results.empty()) {
        std::string pattern = "%" + EscapeLikePattern(request.query) + "%";
        {
            const char* sql = "SELECT id, level, topic, summary, source_refs_json FROM memory_summaries "
                              "WHERE agent_id = ? AND (? = '' OR session_id = ?) AND "
                              "(topic LIKE ? ESCAPE '\\' OR summary LIKE ? ESCAPE '\\') "
                              "ORDER BY updated_at DESC, id DESC LIMIT ?;";
            SQLiteStatement stmt(db_, sql);
            if (!stmt) {
                result.error = SqliteFailure(db_, "SearchLongTermMemory::like_summaries_prepare").error;
                return result;
            }
            sqlite3_bind_text(stmt.get(), 1, request.agentId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 2, request.sessionId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 3, request.sessionId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 4, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 5, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 6, limit);
            int rc = SQLITE_ROW;
            while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
                MemorySearchResult item;
                item.id = "summary:" + std::to_string(sqlite3_column_int(stmt.get(), 0));
                item.type = "summary";
                item.content = ColumnText(stmt.get(), 1) + ": " +
                               ColumnText(stmt.get(), 2) + ": " +
                               ColumnText(stmt.get(), 3);
                item.sourceRefs = ParseSourceRefsColumn(stmt.get(), 4);
                ApplyFallbackScore(item);
                results.push_back(std::move(item));
            }
            if (rc != SQLITE_DONE) {
                result.error = SqliteFailure(db_, "SearchLongTermMemory::like_summaries_step").error;
                return result;
            }
        }
        {
            const char* sql = "SELECT id, agent_id, type, name, summary, source_refs_json, metadata_json FROM memory_entities "
                              "WHERE active = 1 AND agent_id = ? AND "
                              "(id LIKE ? ESCAPE '\\' OR type LIKE ? ESCAPE '\\' OR name LIKE ? ESCAPE '\\' OR summary LIKE ? ESCAPE '\\') "
                              "ORDER BY updated_at DESC LIMIT ?;";
            SQLiteStatement stmt(db_, sql);
            if (!stmt) {
                result.error = SqliteFailure(db_, "SearchLongTermMemory::like_entities_prepare").error;
                return result;
            }
            sqlite3_bind_text(stmt.get(), 1, request.agentId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 4, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 5, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 6, limit);
            int rc = SQLITE_ROW;
            while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
                MemorySearchResult item;
                item.id = ColumnText(stmt.get(), 0);
                item.type = "entity";
                item.content = ColumnText(stmt.get(), 2) + ": " +
                               ColumnText(stmt.get(), 3) + ": " +
                               ColumnText(stmt.get(), 4);
                item.sourceRefs = ParseSourceRefsColumn(stmt.get(), 5);
                item.metadata = ParseJsonColumn(stmt.get(), 6);
                ApplyFallbackScore(item);
                results.push_back(std::move(item));
            }
            if (rc != SQLITE_DONE) {
                result.error = SqliteFailure(db_, "SearchLongTermMemory::like_entities_step").error;
                return result;
            }
        }
        {
            const char* sql = "SELECT id, agent_id, from_entity, relation, to_entity, source_refs_json FROM memory_relations "
                              "WHERE active = 1 AND agent_id = ? AND "
                              "(from_entity LIKE ? ESCAPE '\\' OR relation LIKE ? ESCAPE '\\' OR to_entity LIKE ? ESCAPE '\\') "
                              "ORDER BY updated_at DESC, id DESC LIMIT ?;";
            SQLiteStatement stmt(db_, sql);
            if (!stmt) {
                result.error = SqliteFailure(db_, "SearchLongTermMemory::like_relations_prepare").error;
                return result;
            }
            sqlite3_bind_text(stmt.get(), 1, request.agentId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.get(), 4, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt.get(), 5, limit);
            int rc = SQLITE_ROW;
            while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
                MemorySearchResult item;
                item.id = "relation:" + std::to_string(sqlite3_column_int(stmt.get(), 0));
                item.type = "relation";
                item.content = ColumnText(stmt.get(), 2) + " " +
                               ColumnText(stmt.get(), 3) + " " +
                               ColumnText(stmt.get(), 4);
                item.sourceRefs = ParseSourceRefsColumn(stmt.get(), 5);
                ApplyFallbackScore(item);
                results.push_back(std::move(item));
            }
            if (rc != SQLITE_DONE) {
                result.error = SqliteFailure(db_, "SearchLongTermMemory::like_relations_step").error;
                return result;
            }
        }
    }

    SortAndLimitSearchResults(results, limit);
    result.succeeded = true;
    return result;
}

MemoryStatsResult MemorySqliteStore::GetStoreStats() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryStatsResult result;
    auto& stats = result.stats;
    if (db_ == nullptr) {
        result.error = MemoryFailure("store_unavailable", "sqlite store is not initialized").error;
        return result;
    }

    const char* queries[] = {
        "SELECT COUNT(*) FROM memory_events;",
        "SELECT COUNT(*) FROM memory_payloads;",
        "SELECT COUNT(*) FROM memory_summaries;",
        "SELECT COUNT(*) FROM memory_entities;",
        "SELECT COUNT(*) FROM memory_relations;",
    };
    int* fields[] = {
        &stats.events,
        &stats.payloads,
        &stats.summaries,
        &stats.entities,
        &stats.relations,
    };

    for (int i = 0; i < 5; ++i) {
        SQLiteStatement stmt(db_, queries[i]);
        if (!stmt) {
            result.error = SqliteFailure(db_, "GetStoreStats::prepare").error;
            return result;
        }
        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW) {
            *fields[i] = sqlite3_column_int(stmt.get(), 0);
        } else if (rc != SQLITE_DONE) {
            result.error = SqliteFailure(db_, "GetStoreStats::step").error;
            return result;
        }
    }

    result.succeeded = true;
    return result;
}

bool MemorySqliteStore::IsValidTableName(const std::string& tableName) const
{
    for (const auto& valid : VALID_TABLE_NAMES) {
        if (tableName == valid) {
            return true;
        }
    }
    return false;
}

bool MemorySqliteStore::ExecuteUnlocked(const std::string& sql) const
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        LogSqliteExecError(errMsg, "Execute");
        return false;
    }
    return true;
}

bool MemorySqliteStore::ColumnExistsUnlocked(const std::string& tableName, const std::string& columnName) const
{
    if (!IsValidTableName(tableName)) {
        return false;
    }
    std::string sql = "PRAGMA table_info(" + tableName + ");";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        if (ColumnText(stmt.get(), 1) == columnName) {
            found = true;
            break;
        }
    }
    return found;
}

bool MemorySqliteStore::AddColumnIfMissingUnlocked(const std::string& tableName, const std::string& columnName,
                                                   const std::string& definition) const
{
    if (!IsValidTableName(tableName)) {
        return false;
    }
    if (ColumnExistsUnlocked(tableName, columnName)) {
        return true;
    }
    std::string sql = "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + definition + ";";
    return ExecuteUnlocked(sql);
}

bool MemorySqliteStore::SaveEventUnlocked(const MemoryEvent& event)
{
    const char* sql = "INSERT INTO memory_events "
                      "(agent_id, session_id, event_type, role, content, payload_ref, tool_call_id, tool_name, metadata_json) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }

    sqlite3_bind_text(stmt.get(), 1, event.agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, event.sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, static_cast<int>(event.type));
    sqlite3_bind_text(stmt.get(), 4, event.role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 5, event.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 6, event.payloadRef.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 7, event.toolCallId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 8, event.toolName.c_str(), -1, SQLITE_TRANSIENT);
    std::string metadataJson = event.metadata.is_object() ? event.metadata.dump() : nlohmann::json::object().dump();
    sqlite3_bind_text(stmt.get(), 9, metadataJson.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        LogSqliteError(db_, "SaveEvent::step");
        return false;
    }
    return true;
}

bool MemorySqliteStore::SavePayloadUnlocked(const MemoryPayloadRef& payload)
{
    const char* sql = "INSERT INTO memory_payloads "
                      "(agent_id, session_id, ref, content_type, tool_name, summary, original_chars) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(ref) DO UPDATE SET "
                      "agent_id = excluded.agent_id, "
                      "session_id = excluded.session_id, "
                      "content_type = excluded.content_type, "
                      "tool_name = excluded.tool_name, "
                      "summary = excluded.summary, "
                      "original_chars = excluded.original_chars;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }

    sqlite3_bind_text(stmt.get(), 1, payload.agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, payload.sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, payload.uri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, payload.contentType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 5, payload.toolName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 6, payload.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 7, payload.originalChars);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        LogSqliteError(db_, "SavePayload::step");
        return false;
    }
    return true;
}

bool MemorySqliteStore::SaveSummaryUnlocked(const std::string& agentId, const std::string& sessionId,
                                             const std::string& level, const std::string& topic,
                                             const std::string& summary, float confidence,
                                             const std::vector<std::string>& sourceRefs)
{
    std::string refsStr = SerializeSourceRefs(sourceRefs);

    const char* sql = "INSERT INTO memory_summaries "
                      "(agent_id, session_id, level, topic, summary, confidence, source_refs_json) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }

    sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, level.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 5, summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 6, confidence);
    sqlite3_bind_text(stmt.get(), 7, refsStr.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        LogSqliteError(db_, "SaveSummary::step");
        return false;
    }
    return true;
}

bool MemorySqliteStore::SaveEntityUnlocked(const MemoryEntity& entity)
{
    nlohmann::json metadata = entity.metadata.is_object() ? entity.metadata : nlohmann::json::object();

    std::string refsStr = SerializeSourceRefs(entity.sourceRefs);
    std::string metadataStr = metadata.dump();

    if (!MarkDuplicateEntitiesObsoleteUnlocked(entity)) {
        return false;
    }

    const char* sql = "INSERT INTO memory_entities "
                      "(id, agent_id, type, name, summary, confidence, source_refs_json, metadata_json, active, superseded_by, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
                      "ON CONFLICT(id) DO UPDATE SET "
                      "agent_id = excluded.agent_id, "
                      "type = excluded.type, "
                      "name = excluded.name, "
                      "summary = excluded.summary, "
                      "confidence = excluded.confidence, "
                      "source_refs_json = excluded.source_refs_json, "
                      "metadata_json = excluded.metadata_json, "
                      "active = excluded.active, "
                      "superseded_by = excluded.superseded_by, "
                      "updated_at = CURRENT_TIMESTAMP;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }

    sqlite3_bind_text(stmt.get(), 1, entity.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, entity.agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, entity.entityType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, entity.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 5, entity.summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 6, entity.confidence);
    sqlite3_bind_text(stmt.get(), 7, refsStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 8, metadataStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 9, entity.isActive ? 1 : 0);
    if (entity.supersededByEntityId.empty()) {
        sqlite3_bind_null(stmt.get(), 10);
    } else {
        sqlite3_bind_text(stmt.get(), 10, entity.supersededByEntityId.c_str(), -1, SQLITE_TRANSIENT);
    }

    bool ok = sqlite3_step(stmt.get()) == SQLITE_DONE;
    if (!ok) {
        LogSqliteError(db_, "SaveEntity::step");
        return false;
    }

    if (!entity.supersededEntityId.empty()) {
        MarkEntityObsoleteUnlocked(entity.supersededEntityId, entity.id);
    }

    return true;
}

bool MemorySqliteStore::SaveRelationUnlocked(const MemoryRelation& relation)
{
    std::string refsStr = SerializeSourceRefs(relation.sourceRefs);
    nlohmann::json metadata = relation.metadata.is_object() ? relation.metadata : nlohmann::json::object();
    if (relation.relationType == "contradicts") {
        metadata["conflict"] = true;
        metadata["conflictsWithEntityId"] = relation.toEntityId;
    }
    std::string metadataStr = metadata.dump();

    {
        const char* obsoleteSql = "UPDATE memory_relations SET active = 0, updated_at = CURRENT_TIMESTAMP "
                                  "WHERE from_entity = ? AND relation = ? AND to_entity = ? AND active = 1 AND agent_id = ?;";
        SQLiteStatement obsoleteStmt(db_, obsoleteSql);
        if (obsoleteStmt) {
            sqlite3_bind_text(obsoleteStmt.get(), 1, relation.fromEntityId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(obsoleteStmt.get(), 2, relation.relationType.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(obsoleteStmt.get(), 3, relation.toEntityId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(obsoleteStmt.get(), 4, relation.agentId.c_str(), -1, SQLITE_TRANSIENT);
            int rc = sqlite3_step(obsoleteStmt.get());
            if (rc != SQLITE_DONE && rc != SQLITE_OK) {
                LogSqliteError(db_, "SaveRelation::obsolete_step");
            }
        }
    }

    const char* sql = "INSERT INTO memory_relations "
                      "(agent_id, from_entity, relation, to_entity, confidence, source_refs_json, metadata_json, active) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, 1);";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }

    sqlite3_bind_text(stmt.get(), 1, relation.agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, relation.fromEntityId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, relation.relationType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, relation.toEntityId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 5, relation.confidence);
    sqlite3_bind_text(stmt.get(), 6, refsStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 7, metadataStr.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt.get()) == SQLITE_DONE;
    if (!ok) {
        LogSqliteError(db_, "SaveRelation::step");
        return false;
    }

    if (relation.relationType == "supersedes") {
        MarkEntityObsoleteUnlocked(relation.toEntityId, relation.fromEntityId);
    }

    return true;
}

bool MemorySqliteStore::MarkDuplicateEntitiesObsoleteUnlocked(const MemoryEntity& entity)
{
    if (entity.id.empty() || entity.agentId.empty() || entity.entityType.empty() || entity.name.empty()) {
        return true;
    }

    const char* sql = "UPDATE memory_entities SET active = 0, superseded_by = ?, updated_at = CURRENT_TIMESTAMP "
                      "WHERE agent_id = ? AND type = ? AND name = ? AND id <> ? AND active = 1;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }
    sqlite3_bind_text(stmt.get(), 1, entity.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, entity.agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 3, entity.entityType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 4, entity.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 5, entity.id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        LogSqliteError(db_, "MarkDuplicateEntitiesObsolete::step");
        return false;
    }
    return true;
}

bool MemorySqliteStore::MarkEntityObsoleteUnlocked(const std::string& entityId, const std::string& supersededBy)
{
    const char* sql = "UPDATE memory_entities SET active = 0, superseded_by = ?, "
                       "updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }

    sqlite3_bind_text(stmt.get(), 1, supersededBy.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, entityId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        LogSqliteError(db_, "MarkEntityObsolete::step");
        return false;
    }
    return true;
}

bool MemorySqliteStore::SaveConsolidationCursorUnlocked(const std::string& agentId, const std::string& sessionId, const std::string& cursor)
{
    const char* sql = "INSERT INTO memory_consolidation_cursors (agent_id, session_id, cursor, updated_at) "
                      "VALUES (?, ?, ?, CURRENT_TIMESTAMP) "
                      "ON CONFLICT(agent_id, session_id) DO UPDATE SET cursor = excluded.cursor, "
                      "updated_at = CURRENT_TIMESTAMP;";
    SQLiteStatement stmt(db_, sql);
    if (!stmt) {
        return false;
    }
    int cursorValue = CursorToInt(cursor);
    sqlite3_bind_text(stmt.get(), 1, agentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, cursorValue);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        LogSqliteError(db_, "SaveConsolidationCursor::step");
        return false;
    }
    return true;
}

} // namespace agent_memory