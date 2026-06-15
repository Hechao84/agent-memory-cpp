#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "agent_memory/model_client.h"
#include "consolidation_batch_builder.h"
#include "consolidation_service.h"
#include "llm_processor.h"
#include "memory_update_writer.h"
#include "rule_based_processor.h"
#include "store.h"

using namespace agent_memory;

namespace {

class InMemoryStore : public MemoryStore
{
public:
    bool Initialize() override { return true; }
    bool SaveEvent(const MemoryEvent& event) override
    {
        events.push_back(event);
        return true;
    }
    bool SavePayload(const MemoryPayloadRef& payload) override
    {
        payloads.push_back(payload);
        return true;
    }
    bool SaveSummary(const std::string&, const std::string&, const std::string& level, const std::string&,
                     const std::string& summary, float, const std::vector<std::string>&) override
    {
        summaryLevels.push_back(level);
        summaries.push_back(summary);
        return true;
    }
    bool SaveEntity(const MemoryEntity& entity) override
    {
        entities.push_back(entity);
        return !failEntitySave;
    }
    bool SaveRelation(const MemoryRelation& relation) override
    {
        relations.push_back(relation);
        return true;
    }
    bool MarkEntityObsolete(const std::string&, const std::string&) override { return true; }
    bool RunInTransaction(const std::function<bool()>& work) override { return work(); }
    std::string LoadConsolidationCursor(const std::string& agentId, const std::string& sessionId) const override
    {
        auto it = cursors.find(agentId + ":" + sessionId);
        return it == cursors.end() ? std::string() : it->second;
    }
    bool SaveConsolidationCursor(const std::string& agentId, const std::string& sessionId, const std::string& cursor) override
    {
        cursors[agentId + ":" + sessionId] = cursor;
        return true;
    }
    std::vector<MemoryEvent> LoadEventsAfterCursor(const std::string& agentId, const std::string& sessionId,
                                                    const std::string& cursor) const override
    {
        std::vector<MemoryEvent> result;
        size_t start = cursor.empty() ? 0 : static_cast<size_t>(std::stoi(cursor));
        for (size_t i = start; i < events.size(); ++i) {
            if (!agentId.empty() && events[i].agentId != agentId) {
                continue;
            }
            if (!sessionId.empty() && events[i].sessionId != sessionId) {
                continue;
            }
            MemoryEvent event = events[i];
            event.storeCursor = std::to_string(i + 1);
            result.push_back(event);
        }
        return result;
    }
    std::vector<MemoryEvent> LoadRecentEvents(const std::string& agentId, const std::string& sessionId,
                                              int limit) const override
    {
        std::vector<MemoryEvent> result;
        for (const auto& event : events) {
            if (!agentId.empty() && event.agentId != agentId) {
                continue;
            }
            if (!sessionId.empty() && event.sessionId != sessionId) {
                continue;
            }
            result.push_back(event);
        }
        if (limit > 0 && static_cast<int>(result.size()) > limit) {
            return std::vector<MemoryEvent>(result.end() - limit, result.end());
        }
        return result;
    }
    LongTermMemorySnapshot LoadLongTermMemory(const std::string&, int, const std::string&) const override { return {}; }
    std::vector<MemoryPayloadRef> LoadRecentPayloads(const std::string& agentId, const std::string& sessionId, int limit) const override
    {
        std::vector<MemoryPayloadRef> result;
        for (const auto& payload : payloads) {
            if (payload.agentId != agentId || (!sessionId.empty() && payload.sessionId != sessionId)) {
                continue;
            }
            result.push_back(payload);
        }
        if (limit <= 0 || static_cast<int>(result.size()) <= limit) {
            return result;
        }
        return std::vector<MemoryPayloadRef>(result.end() - limit, result.end());
    }
    std::vector<MemorySearchResult> SearchLongTermMemory(const MemorySearchRequest&) const override { return {}; }
    MemoryStats GetStoreStats() const override
    {
        MemoryStats stats;
        stats.events = static_cast<int>(events.size());
        stats.payloads = static_cast<int>(payloads.size());
        stats.summaries = static_cast<int>(summaries.size());
        stats.entities = static_cast<int>(entities.size());
        stats.relations = static_cast<int>(relations.size());
        return stats;
    }

    std::vector<MemoryEvent> events;
    std::vector<MemoryPayloadRef> payloads;
    std::vector<std::string> summaryLevels;
    std::vector<std::string> summaries;
    std::vector<MemoryEntity> entities;
    std::vector<MemoryRelation> relations;
    mutable std::map<std::string, std::string> cursors;
    bool failEntitySave{false};
};

class StaticModelClient : public ModelClient
{
public:
    explicit StaticModelClient(std::string response)
        : response_(std::move(response))
    {
    }

    ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) override
    {
        lastPrompt = prompt;
        ModelInvokeResult result;
        result.text = response_;
        return result;
    }

    std::string lastPrompt;

private:
    std::string response_;
};

MemoryEvent Message(const std::string& role, const std::string& content, const std::string& sessionId = "session-1")
{
    MemoryEvent event;
    event.type = MemoryEventType::MESSAGE_APPENDED;
    event.agentId = "agent-1";
    event.sessionId = sessionId;
    event.role = role;
    event.content = content;
    return event;
}

bool HasSummaryLevel(const InMemoryStore& store, const std::string& level)
{
    for (const auto& item : store.summaryLevels) {
        if (item == level) {
            return true;
        }
    }
    return false;
}

bool TestBatchBuilder()
{
    ConsolidationBatchBuilder builder;
    std::vector<MemoryEvent> events = {
        Message("user", "first", "session-1"),
        Message("assistant", "second", "session-2"),
        Message("user", "third", "session-1"),
    };
    MemoryEvent toolEvent = Message("tool", "ignored", "session-1");
    toolEvent.type = MemoryEventType::TOOL_CALL_FINISHED;
    events.push_back(toolEvent);

    MemoryConsolidationRequest request;
    request.sessionId = "session-1";
    request.maxEvents = 1;
    auto result = builder.Build(request, events);
    if (result.batch.events.size() != 1 || result.sessionSummary.find("first") == std::string::npos ||
        result.batch.sourceRefs[0] != "session://session-1#event:1") {
        std::cerr << "batch builder failed session/maxEvents filtering\n";
        return false;
    }

    request.maxEvents = 0;
    request.forceReprocess = false;
    result = builder.Build(request, events);
    if (!result.batch.events.empty()) {
        std::cerr << "batch builder ignored maxEvents=0 without force\n";
        return false;
    }

    request.forceReprocess = true;
    result = builder.Build(request, events);
    if (result.batch.events.size() != 2) {
        std::cerr << "batch builder failed force maxEvents=0 behavior\n";
        return false;
    }
    return true;
}

bool TestRuleBasedProcessor()
{
    RuleBasedLongTermMemoryProcessor processor;
    LongTermMemoryBatch batch;
    batch.events.push_back(Message("user", "I prefer concise answers about code testing"));
    batch.sourceRefs.push_back("session://session-1#event:1");

    LongTermMemoryUpdate update = processor.Process(batch);
    if (update.topicSummaries.empty() || update.entities.size() < 2 || update.relations.empty()) {
        std::cerr << "rule-based processor failed to extract topic/preference\n";
        return false;
    }
    return true;
}

bool TestMemoryUpdateWriter()
{
    InMemoryStore store;
    MemoryUpdateWriter writer(store);

    LongTermMemoryUpdate update;
    update.topicSummaries.push_back("Discussed topic: testing");
    update.profileSummaries.push_back("User prefers concise answers");
    MemoryEntity entity;
    entity.id = "entity:test";
    entity.agentId = "agent-1";
    update.entities.push_back(entity);
    MemoryRelation relation;
    relation.fromEntityId = "entity:user";
    relation.toEntityId = "entity:test";
    relation.relationType = "mentions";
    relation.agentId = "agent-1";
    update.relations.push_back(relation);

    auto sessionWrite = writer.SaveSessionSummary("agent-1", "session-1", "user: hello", {"session://session-1"});
    if (!sessionWrite.succeeded || sessionWrite.savedSummaries != 1) {
        std::cerr << "writer failed to save session summary\n";
        return false;
    }
    auto updateWrite = writer.SaveUpdate("agent-1", "session-1", update, {"session://session-1"});
    if (!updateWrite.succeeded || updateWrite.savedSummaries != 2 || updateWrite.savedEntities != 1 ||
        updateWrite.savedRelations != 1) {
        std::cerr << "writer failed to save update\n";
        return false;
    }
    if (!HasSummaryLevel(store, "session") || !HasSummaryLevel(store, "topic") || !HasSummaryLevel(store, "profile") ||
        store.entities.size() != 1 || store.relations.size() != 1) {
        std::cerr << "writer saved incomplete update\n";
        return false;
    }
    return true;
}

bool TestMemoryUpdateWriterFailureStopsUpdate()
{
    InMemoryStore store;
    store.failEntitySave = true;
    MemoryUpdateWriter writer(store);

    LongTermMemoryUpdate update;
    MemoryEntity entity;
    entity.id = "entity:test";
    entity.agentId = "agent-1";
    update.entities.push_back(entity);
    MemoryRelation relation;
    relation.fromEntityId = "entity:user";
    relation.toEntityId = "entity:test";
    relation.relationType = "mentions";
    relation.agentId = "agent-1";
    update.relations.push_back(relation);

    auto write = writer.SaveUpdate("agent-1", "session-1", update, {"session://session-1#event:1"});
    if (write.succeeded || write.savedEntities != 0 || write.savedRelations != 0) {
        std::cerr << "writer should fail without reporting partial saves\n";
        return false;
    }
    if (store.entities.size() != 1 || !store.relations.empty()) {
        std::cerr << "writer should stop after entity save failure\n";
        return false;
    }
    return true;
}

bool TestConsolidationFallback()
{
    InMemoryStore store;
    MemoryUpdateWriter writer(store);
    RuleBasedLongTermMemoryProcessor fallback;
    ConsolidationService service(writer, &fallback);

    MemoryConsolidationRequest request;
    request.agentId = "agent-1";
    request.sessionId = "session-1";
    request.maxEvents = 10;

    std::vector<MemoryEvent> events = {Message("user", "I prefer tests for database code")};
    auto result = service.Consolidate(request, events, nullptr);
    if (!result || !result.fallbackUsed || result.processedEvents != 1 || result.savedSummaries < 1 ||
        result.savedEntities < 1 || result.nextCursor.empty()) {
        std::cerr << "fallback consolidation returned incomplete result\n";
        return false;
    }
    if (!HasSummaryLevel(store, "session") || store.entities.empty()) {
        std::cerr << "fallback consolidation did not persist rule-based update\n";
        return false;
    }
    return true;
}

bool TestLlmProcessorEdgeCases()
{
    LongTermMemoryBatch batch;
    batch.events.push_back(Message("user", "Remember this"));
    batch.sourceRefs.push_back("session://session-1#event:1");

    StaticModelClient invalidModel("not json");
    LlmLongTermMemoryProcessor invalidProcessor(&invalidModel);
    LongTermMemoryUpdate invalidUpdate = invalidProcessor.Process(batch);
    if (!invalidUpdate.topicSummaries.empty() || !invalidUpdate.entities.empty()) {
        std::cerr << "LLM processor should ignore invalid JSON\n";
        return false;
    }

    StaticModelClient markdownModel("```json\n{\"entities\":[{\"entityType\":\"topic\",\"name\":\"No ID\"}],\"relations\":[{\"fromEntityId\":\"a\",\"toEntityId\":\"b\"}]}\n```");
    LlmLongTermMemoryProcessor markdownProcessor(&markdownModel);
    LongTermMemoryUpdate markdownUpdate = markdownProcessor.Process(batch);
    if (markdownUpdate.entities.size() != 1 || markdownUpdate.entities[0].id != "entity:llm.0" ||
        markdownUpdate.entities[0].sourceRefs.empty() || markdownUpdate.relations.size() != 1 ||
        markdownUpdate.relations[0].relationType != "related_to") {
        std::cerr << "LLM processor failed markdown/default/fallback sourceRefs case\n";
        return false;
    }
    return true;
}

bool TestLlmEmptyUpdateFallback()
{
    InMemoryStore store;
    MemoryUpdateWriter writer(store);
    RuleBasedLongTermMemoryProcessor fallback;
    ConsolidationService service(writer, &fallback);
    StaticModelClient emptyModel("{}");

    MemoryConsolidationRequest request;
    request.agentId = "agent-1";
    request.sessionId = "session-1";
    request.maxEvents = 10;

    std::vector<MemoryEvent> events = {Message("user", "I prefer database tests")};
    auto result = service.Consolidate(request, events, &emptyModel);
    if (!result || !result.fallbackUsed || result.savedEntities < 1) {
        std::cerr << "empty LLM update fallback returned false\n";
        return false;
    }
    if (store.entities.empty()) {
        std::cerr << "empty LLM update did not fallback to rule-based processor\n";
        return false;
    }
    return true;
}

bool TestLlmProcessorAndConsolidation()
{
    std::string response = R"({
        "topicSummaries": ["Discussed API design"],
        "profileSummaries": ["User wants concise answers"],
        "entities": [{"id":"entity:api","entityType":"topic","name":"API","summary":"API discussion","confidence":0.8}],
        "relations": [{"fromEntityId":"entity:user","relationType":"mentions","toEntityId":"entity:api","confidence":0.7}]
    })";
    StaticModelClient model(response);

    LlmLongTermMemoryProcessor processor(&model);
    LongTermMemoryBatch batch;
    batch.events.push_back(Message("user", "Let's discuss API design"));
    batch.sourceRefs.push_back("session://session-1#event:1");
    LongTermMemoryUpdate update = processor.Process(batch);
    if (update.topicSummaries.size() != 1 || update.profileSummaries.size() != 1 ||
        update.entities.size() != 1 || update.relations.size() != 1) {
        std::cerr << "LLM processor failed to parse response\n";
        return false;
    }
    if (model.lastPrompt.find("session://session-1#event:1") == std::string::npos) {
        std::cerr << "LLM processor prompt missing source reference\n";
        return false;
    }

    InMemoryStore store;
    MemoryUpdateWriter writer(store);
    RuleBasedLongTermMemoryProcessor fallback;
    ConsolidationService service(writer, &fallback);
    MemoryConsolidationRequest request;
    request.agentId = "agent-1";
    request.sessionId = "session-1";
    request.maxEvents = 10;

    std::vector<MemoryEvent> events = {Message("user", "Let's discuss API design")};
    auto result = service.Consolidate(request, events, &model);
    if (!result || result.fallbackUsed || result.processedEvents != 1 || result.savedEntities != 1 ||
        result.savedRelations != 1) {
        std::cerr << "LLM consolidation returned incomplete result\n";
        return false;
    }
    if (!HasSummaryLevel(store, "session") || !HasSummaryLevel(store, "topic") ||
        !HasSummaryLevel(store, "profile") || store.entities.size() != 1 || store.relations.size() != 1) {
        std::cerr << "LLM consolidation did not persist LLM update\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!TestBatchBuilder()) {
        return 1;
    }
    if (!TestRuleBasedProcessor()) {
        return 1;
    }
    if (!TestMemoryUpdateWriter()) {
        return 1;
    }
    if (!TestMemoryUpdateWriterFailureStopsUpdate()) {
        return 1;
    }
    if (!TestConsolidationFallback()) {
        return 1;
    }
    if (!TestLlmProcessorEdgeCases()) {
        return 1;
    }
    if (!TestLlmEmptyUpdateFallback()) {
        return 1;
    }
    if (!TestLlmProcessorAndConsolidation()) {
        return 1;
    }
    return 0;
}