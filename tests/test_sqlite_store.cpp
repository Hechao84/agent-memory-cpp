#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "memory_update_writer.h"
#include "sqlite_store.h"

using namespace agent_memory;

namespace {

std::filesystem::path TempDbPath(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / "agent-memory-sqlite-tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir / name;
}

MemoryEvent MakeEvent(const std::string& agentId, const std::string& sessionId, const std::string& role,
                      const std::string& content)
{
    MemoryEvent event;
    event.type = MemoryEventType::MESSAGE_APPENDED;
    event.agentId = agentId;
    event.sessionId = sessionId;
    event.role = role;
    event.content = content;
    event.metadata["source"] = "sqlite-test";
    event.metadata["content"] = content;
    return event;
}

bool TestEventsAndCursors()
{
    MemorySqliteStore store(TempDbPath("events.db").string());
    if (!store.Initialize()) {
        std::cerr << "sqlite initialize failed\n";
        return false;
    }

    if (!store.SaveEvent(MakeEvent("agent-1", "session-1", "user", "first")) ||
        !store.SaveEvent(MakeEvent("agent-1", "session-2", "assistant", "second")) ||
        !store.SaveEvent(MakeEvent("agent-2", "session-1", "user", "third"))) {
        std::cerr << "save event failed\n";
        return false;
    }

    auto sessionEvents = store.LoadEventsAfterCursor("agent-1", "session-1", "");
    if (sessionEvents.size() != 1 || sessionEvents[0].content != "first" || sessionEvents[0].storeCursor.empty() ||
        sessionEvents[0].metadata["source"] != "sqlite-test" || sessionEvents[0].metadata["content"] != "first") {
        std::cerr << "session event query failed\n";
        return false;
    }

    auto globalEvents = store.LoadEventsAfterCursor("agent-1", "", "");
    if (globalEvents.size() != 2) {
        std::cerr << "global event query failed\n";
        return false;
    }

    auto afterCursor = store.LoadEventsAfterCursor("agent-1", "", globalEvents[0].storeCursor);
    if (afterCursor.size() != 1 || afterCursor[0].content != "second") {
        std::cerr << "cursor event query failed\n";
        return false;
    }

    auto recent = store.LoadRecentEvents("agent-1", "", 1);
    if (recent.size() != 1 || recent[0].content != "second" || recent[0].metadata["content"] != "second") {
        std::cerr << "recent event query failed\n";
        return false;
    }

    if (!store.SaveConsolidationCursor("agent-1", "", "42") ||
        store.LoadConsolidationCursor("agent-1", "").cursor != "42") {
        std::cerr << "cursor save/load failed\n";
        return false;
    }

    auto stats = store.GetStoreStats();
    if (stats.stats.events != 3) {
        std::cerr << "event stats failed\n";
        return false;
    }

    // Excluded sessions filter at SQL layer: agent-level query (empty
    // sessionId) with __CRON__/__HEARTBEAT__ in the exclude set must skip
    // them while still returning other sessions.
    MemoryEvent cronEvent = MakeEvent("agent-1", "__CRON__", "user", "cron tick");
    MemoryEvent heartbeatEvent = MakeEvent("agent-1", "__HEARTBEAT__", "user", "heartbeat tick");
    if (!store.SaveEvent(cronEvent) || !store.SaveEvent(heartbeatEvent)) {
        std::cerr << "save excluded event failed\n";
        return false;
    }
    auto filtered = store.LoadEventsAfterCursor("agent-1", "", "",
                                                std::vector<std::string>{"__CRON__", "__HEARTBEAT__"});
    if (!filtered.succeeded) {
        std::cerr << "excluded-session query failed: " << filtered.error.code << "\n";
        return false;
    }
    for (const auto& ev : filtered.events) {
        if (ev.sessionId == "__CRON__" || ev.sessionId == "__HEARTBEAT__") {
            std::cerr << "excluded session event should not be returned: " << ev.sessionId << "\n";
            return false;
        }
    }
    if (filtered.events.size() != 2) {  // session-1 + session-2 from before
        std::cerr << "excluded-session query returned wrong count: " << filtered.events.size() << "\n";
        return false;
    }

    // Empty exclude set: backward compatibility, all events come back.
    auto all = store.LoadEventsAfterCursor("agent-1", "", "");
    if (all.events.size() != 4) {
        std::cerr << "empty exclude set should return all events\n";
        return false;
    }

    // sqlWithSession path (non-empty sessionId) ignores excludedSessionIds:
    // session_id = ? already locks to a single session, so the NOT IN
    // clause is intentionally not appended to that SQL branch. Asking
    // for session-1 events while "excluding" session-1 (and others) must
    // still return the session-1 events -- the exclude set is a no-op on
    // this code path. This locks the design decision so a future refactor
    // does not silently start filtering single-session queries.
    auto sessionScoped = store.LoadEventsAfterCursor(
        "agent-1", "session-1", "",
        std::vector<std::string>{"session-1", "__CRON__", "__HEARTBEAT__"});
    if (!sessionScoped.succeeded) {
        std::cerr << "session-scoped query with exclude set failed: "
                  << sessionScoped.error.code << "\n";
        return false;
    }
    if (sessionScoped.events.size() != 1 || sessionScoped.events[0].content != "first") {
        std::cerr << "session-scoped query must ignore excludedSessionIds\n";
        return false;
    }
    return true;
}

bool TestPayloads()
{
    MemorySqliteStore store(TempDbPath("payloads.db").string());
    if (!store.Initialize()) {
        std::cerr << "sqlite initialize failed\n";
        return false;
    }

    MemoryPayloadRef first;
    first.agentId = "agent-1";
    first.sessionId = "session-1";
    first.uri = "file://payload-1.txt";
    first.contentType = "text/plain";
    first.summary = "first payload";
    first.toolName = "tool-a";
    first.originalChars = 10;

    MemoryPayloadRef second;
    second.agentId = "agent-1";
    second.sessionId = "session-1";
    second.uri = "file://payload-2.txt";
    second.contentType = "text/plain";
    second.summary = "second payload";
    second.toolName = "tool-b";
    second.originalChars = 20;

    if (!store.SavePayload(first) || !store.SavePayload(second)) {
        std::cerr << "payload save failed\n";
        return false;
    }

    auto recent = store.LoadRecentPayloads("agent-1", "session-1", 1);
    if (recent.size() != 1 || recent[0].uri != second.uri || recent[0].agentId != "agent-1" || recent[0].sessionId != "session-1") {
        std::cerr << "recent payload limit failed\n";
        return false;
    }

    auto all = store.LoadRecentPayloads("agent-1", "", 0);
    if (all.size() != 2) {
        std::cerr << "recent payload all failed\n";
        return false;
    }

    auto stats = store.GetStoreStats();
    if (stats.stats.payloads != 2) {
        std::cerr << "payload stats failed\n";
        return false;
    }
    return true;
}

bool TestLongTermMemoryAndSearch()
{
    MemorySqliteStore store(TempDbPath("long_term.db").string());
    if (!store.Initialize()) {
        std::cerr << "sqlite initialize failed\n";
        return false;
    }

    if (!store.SaveSummary("agent-1", "session-1", "topic", "testing", "Testing strategy summary", 0.8F,
                           {"event://summary"})) {
        std::cerr << "summary save failed\n";
        return false;
    }

    MemoryEntity entity;
    entity.id = "entity:topic.testing";
    entity.agentId = "agent-1";
    entity.entityType = "topic";
    entity.name = "testing topic";
    entity.summary = "Testing strategy and coverage";
    entity.confidence = 0.9F;
    entity.sourceRefs = {"event://entity"};

    MemoryEntity oldEntity;
    oldEntity.id = "entity:topic.old";
    oldEntity.agentId = "agent-1";
    oldEntity.entityType = "topic";
    oldEntity.name = "old topic";
    oldEntity.summary = "Old strategy";
    oldEntity.confidence = 0.4F;

    MemoryRelation relation;
    relation.id = "relation-1";
    relation.agentId = "agent-1";
    relation.fromEntityId = "entity:user";
    relation.relationType = "mentions";
    relation.toEntityId = entity.id;
    relation.confidence = 0.7F;
    relation.sourceRefs = {"event://relation"};

    if (!store.SaveEntity(oldEntity) || !store.SaveEntity(entity) || !store.SaveRelation(relation)) {
        std::cerr << "entity/relation save failed\n";
        return false;
    }

    auto snapshot = store.LoadLongTermMemory("agent-1", 10, "session-1");
    if (snapshot.snapshot.summaries.size() != 1 || snapshot.snapshot.entities.size() != 2 || snapshot.snapshot.relations.size() != 1) {
        std::cerr << "long-term snapshot failed\n";
        return false;
    }

    MemorySearchRequest search;
    search.agentId = "agent-1";
    search.sessionId = "session-1";
    search.query = "testing";
    search.limit = 10;
    auto results = store.SearchLongTermMemory(search);
    bool foundEntity = false;
    bool foundSummary = false;
    for (const auto& result : results) {
        if (result.score <= 0.0F || result.metadata.value("scoreSource", "") != "fts_bm25") {
            std::cerr << "search score metadata failed\n";
            return false;
        }
        if (result.type == "entity" && result.id == entity.id) {
            foundEntity = true;
        }
        if (result.type == "summary" && result.content.find("Testing strategy") != std::string::npos) {
            foundSummary = true;
        }
    }
    search.limit = 1;
    auto limitedResults = store.SearchLongTermMemory(search);
    if (limitedResults.size() != 1) {
        std::cerr << "search limit should apply after merged ranking\n";
        return false;
    }
    if (!foundEntity || !foundSummary) {
        std::cerr << "long-term search failed\n";
        return false;
    }

    MemoryEntity replacement = entity;
    replacement.id = "entity:topic.testing.v2";
    replacement.summary = "Updated testing strategy";
    if (!store.SaveEntity(replacement)) {
        std::cerr << "replacement entity save failed\n";
        return false;
    }
    auto dedupSnapshot = store.LoadLongTermMemory("agent-1", 10, "session-1");
    for (const auto& activeEntity : dedupSnapshot.snapshot.entities) {
        if (activeEntity.id == entity.id) {
            std::cerr << "duplicate entity should be obsolete\n";
            return false;
        }
    }

    MemoryRelation conflict;
    conflict.id = "relation-conflict";
    conflict.agentId = "agent-1";
    conflict.fromEntityId = replacement.id;
    conflict.relationType = "contradicts";
    conflict.toEntityId = oldEntity.id;
    conflict.confidence = 0.8F;
    if (!store.SaveRelation(conflict)) {
        std::cerr << "conflict relation save failed\n";
        return false;
    }
    auto conflictSnapshot = store.LoadLongTermMemory("agent-1", 10, "session-1");
    bool foundConflict = false;
    for (const auto& activeRelation : conflictSnapshot.snapshot.relations) {
        if (activeRelation.relationType == "contradicts" && activeRelation.metadata.value("conflict", false)) {
            foundConflict = true;
        }
    }
    if (!foundConflict) {
        std::cerr << "conflict relation metadata failed\n";
        return false;
    }

    if (!store.MarkEntityObsolete(oldEntity.id, replacement.id)) {
        std::cerr << "mark obsolete failed\n";
        return false;
    }
    auto activeSnapshot = store.LoadLongTermMemory("agent-1", 10, "session-1");
    for (const auto& activeEntity : activeSnapshot.snapshot.entities) {
        if (activeEntity.id == oldEntity.id) {
            std::cerr << "obsolete entity should not be active\n";
            return false;
        }
    }

    auto stats = store.GetStoreStats();
    if (stats.stats.summaries != 1 || stats.stats.entities != 3 || stats.stats.relations != 2) {
        std::cerr << "long-term stats failed\n";
        return false;
    }
    return true;
}

bool TestSearchLikeFallbackEscapesWildcards()
{
    MemorySqliteStore store(TempDbPath("like_escape.db").string());
    if (!store.Initialize()) {
        std::cerr << "sqlite initialize failed\n";
        return false;
    }

    if (!store.SaveSummary("agent-1", "session-1", "topic", "ordinary", "ordinary summary", 0.8F,
                           {"event://ordinary"}) ||
        !store.SaveSummary("agent-1", "session-1", "topic", "literal_percent", "contains 100% marker", 0.8F,
                           {"event://percent"})) {
        std::cerr << "summary save failed for like escape test\n";
        return false;
    }

    MemorySearchRequest search;
    search.agentId = "agent-1";
    search.sessionId = "session-1";
    search.query = "%";
    search.limit = 10;
    auto results = store.SearchLongTermMemory(search);
    if (results.size() != 1 || results[0].content.find("100% marker") == std::string::npos ||
        results[0].metadata.value("scoreSource", "") != "like_fallback") {
        std::cerr << "LIKE fallback should treat percent as a literal\n";
        return false;
    }
    return true;
}

bool TestWriterTransactionRollback()
{
    MemorySqliteStore store(TempDbPath("writer_rollback.db").string());
    if (!store.Initialize()) {
        std::cerr << "sqlite initialize failed\n";
        return false;
    }

    MemoryUpdateWriter writer(store);
    MemoryUpdateWriteResult sessionWrite;
    MemoryUpdateWriteResult updateWrite;
    LongTermMemoryUpdate update;
    update.topicSummaries.push_back("should roll back");
    MemoryEntity entity;
    entity.id = "entity:fail";
    entity.agentId = "agent-1";
    entity.entityType = "topic";
    entity.name = "fail topic";
    update.entities.push_back(entity);

    auto ok = writer.RunInTransaction([&](MemoryStoreTransaction& transaction) {
        sessionWrite = writer.SaveSessionSummary(transaction, "agent-1", "session-1", "session summary", {"event://1"});
        updateWrite = writer.SaveUpdate(transaction, "agent-1", "session-1", update, {"event://1"});
        return MemoryFailure("requested_rollback", "requested rollback");
    });
    if (ok || !sessionWrite.succeeded || !updateWrite.succeeded) {
        std::cerr << "writer transaction should roll back requested failure\n";
        return false;
    }
    auto stats = store.GetStoreStats();
    if (stats.stats.summaries != 0 || stats.stats.entities != 0) {
        std::cerr << "writer transaction should roll back partial writes\n";
        return false;
    }
    return true;
}

bool TestUninitializedTransactionDoesNotInitialize()
{
    MemorySqliteStore store(TempDbPath("uninitialized-transaction.db").string());
    bool called = false;
    auto ok = store.RunInTransaction([&](MemoryStoreTransaction&) {
        called = true;
        return MemorySuccess();
    });
    if (ok || called) {
        std::cerr << "uninitialized transaction should fail without invoking work\n";
        return false;
    }
    MemorySearchRequest search;
    search.agentId = "agent-1";
    search.sessionId = "session-1";
    search.query = "query";
    search.limit = 10;
    if (store.LoadRecentEvents("agent-1", "session-1", 10) || store.LoadRecentPayloads("agent-1", "session-1", 10) ||
        store.LoadConsolidationCursor("agent-1", "session-1") || store.LoadLongTermMemory("agent-1", 10) ||
        store.SearchLongTermMemory(search) || store.GetStoreStats()) {
        std::cerr << "uninitialized read methods should fail\n";
        return false;
    }
    if (store.GetStoreStats().error.code != "store_unavailable") {
        std::cerr << "uninitialized read should expose structured error\n";
        return false;
    }
    return true;
}

bool TestTransactions()
{
    MemorySqliteStore store(TempDbPath("transactions.db").string());
    if (!store.Initialize()) {
        std::cerr << "sqlite initialize failed\n";
        return false;
    }

    auto ok = store.RunInTransaction([&](MemoryStoreTransaction& transaction) {
        return transaction.SaveSummary("agent-1", "session-1", "session", "conversation", "inside transaction", 0.5F);
    });
    if (!ok || store.GetStoreStats().stats.summaries != 1) {
        std::cerr << "transaction commit failed\n";
        return false;
    }

    auto failed = store.RunInTransaction([&](MemoryStoreTransaction& transaction) {
        transaction.SaveSummary("agent-1", "session-1", "session", "conversation", "rolled back", 0.5F);
        return MemoryFailure("requested_rollback", "requested rollback");
    });
    if (failed || store.GetStoreStats().stats.summaries != 1) {
        std::cerr << "transaction rollback failed\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!TestEventsAndCursors() || !TestPayloads() || !TestLongTermMemoryAndSearch() ||
        !TestSearchLikeFallbackEscapesWildcards() || !TestWriterTransactionRollback() ||
        !TestUninitializedTransactionDoesNotInitialize() || !TestTransactions()) {
        return 1;
    }
    return 0;
}
