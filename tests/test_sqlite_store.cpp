#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "memory_update_writer.h"
#include "sqlite_store.h"

using namespace agent_memory;

namespace {

class FailingEntitySqliteStore : public MemorySqliteStore
{
public:
    explicit FailingEntitySqliteStore(std::string dbPath)
        : MemorySqliteStore(std::move(dbPath))
    {
    }

    bool SaveEntity(const MemoryEntity&) override { return false; }
};

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
        store.LoadConsolidationCursor("agent-1", "") != "42") {
        std::cerr << "cursor save/load failed\n";
        return false;
    }

    auto stats = store.GetStoreStats();
    if (stats.events != 3) {
        std::cerr << "event stats failed\n";
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
    if (stats.payloads != 2) {
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
    if (snapshot.summaries.size() != 1 || snapshot.entities.size() != 2 || snapshot.relations.size() != 1) {
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
        if (result.type == "entity" && result.id == entity.id) {
            foundEntity = true;
        }
        if (result.type == "summary" && result.content.find("Testing strategy") != std::string::npos) {
            foundSummary = true;
        }
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
    for (const auto& activeEntity : dedupSnapshot.entities) {
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
    for (const auto& activeRelation : conflictSnapshot.relations) {
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
    for (const auto& activeEntity : activeSnapshot.entities) {
        if (activeEntity.id == oldEntity.id) {
            std::cerr << "obsolete entity should not be active\n";
            return false;
        }
    }

    auto stats = store.GetStoreStats();
    if (stats.summaries != 1 || stats.entities != 3 || stats.relations != 2) {
        std::cerr << "long-term stats failed\n";
        return false;
    }
    return true;
}

bool TestWriterTransactionRollback()
{
    FailingEntitySqliteStore store(TempDbPath("writer_rollback.db").string());
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

    bool ok = writer.RunInTransaction([&]() {
        sessionWrite = writer.SaveSessionSummary("agent-1", "session-1", "session summary", {"event://1"});
        updateWrite = writer.SaveUpdate("agent-1", "session-1", update, {"event://1"});
        return sessionWrite.succeeded && updateWrite.succeeded;
    });
    if (ok || updateWrite.succeeded) {
        std::cerr << "writer transaction should fail\n";
        return false;
    }
    auto stats = store.GetStoreStats();
    if (stats.summaries != 0 || stats.entities != 0) {
        std::cerr << "writer transaction should roll back partial writes\n";
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

    bool ok = store.RunInTransaction([&]() {
        return store.SaveEvent(MakeEvent("agent-1", "session-1", "user", "inside transaction"));
    });
    if (!ok || store.LoadEventsAfterCursor("agent-1", "session-1", "").size() != 1) {
        std::cerr << "transaction commit failed\n";
        return false;
    }

    bool failed = store.RunInTransaction([&]() {
        store.SaveEvent(MakeEvent("agent-1", "session-1", "user", "rolled back"));
        return false;
    });
    if (failed || store.LoadEventsAfterCursor("agent-1", "session-1", "").size() != 1) {
        std::cerr << "transaction rollback failed\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!TestEventsAndCursors() || !TestPayloads() || !TestLongTermMemoryAndSearch() ||
        !TestWriterTransactionRollback() || !TestTransactions()) {
        return 1;
    }
    return 0;
}
