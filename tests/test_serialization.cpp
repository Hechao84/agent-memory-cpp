#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "json_memory_codec.h"
#include <nlohmann/json.hpp>

using namespace agent_memory;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool TestEventRoundTrip()
{
    MemoryEvent event;
    event.type = MemoryEventType::TOOL_CALL_FINISHED;
    event.agentId = "agent";
    event.sessionId = "session";
    event.role = "tool";
    event.content = "content";
    event.toolCallId = "call";
    event.toolName = "tool";
    event.payloadRef = "file://payload";
    event.metadata["k"] = "v";
    event.metadata["nested"] = {{"x", 1}};
    event.timestamp = "2026-01-01T00:00:00Z";

    auto decoded = EventFromJson(EventToJson(event));
    return Check(decoded.type == event.type && decoded.agentId == event.agentId && decoded.sessionId == event.sessionId &&
                     decoded.role == event.role && decoded.content == event.content && decoded.toolCallId == event.toolCallId &&
                     decoded.toolName == event.toolName && decoded.payloadRef == event.payloadRef && decoded.metadata == event.metadata &&
                     decoded.timestamp == event.timestamp,
                 "event round-trip failed");
}

bool TestContextRoundTrip()
{
    MemoryContextPackage pkg;
    MemoryMessage message;
    message.role = "assistant";
    message.content = "hello";
    message.toolCallId = "call";
    message.toolName = "tool";
    message.payloadRef = "payload";
    pkg.messages.push_back(message);
    pkg.memoryText = "memory";
    MemoryEntity entity;
    entity.id = "entity";
    entity.entityType = "topic";
    entity.name = "Serialization";
    entity.summary = "summary";
    entity.confidence = 0.8F;
    entity.sourceRefs.push_back("source");
    entity.metadata["mk"] = "mv";
    entity.createdAt = "created";
    entity.updatedAt = "updated";
    pkg.entities.push_back(entity);
    MemoryRelation relation;
    relation.id = "relation";
    relation.fromEntityId = "entity";
    relation.relationType = "references";
    relation.toEntityId = "payload";
    relation.confidence = 0.7F;
    relation.sourceRefs.push_back("source");
    relation.metadata["rk"] = "rv";
    pkg.relations.push_back(relation);
    MemoryPayloadRef payload;
    payload.agentId = "agent";
    payload.sessionId = "session";
    payload.uri = "file://payload";
    payload.contentType = "text/plain";
    payload.summary = "payload summary";
    payload.toolName = "tool";
    payload.originalChars = 42;
    payload.metadata["pk"] = "pv";
    payload.createdAt = "created";
    pkg.payloadRefs.push_back(payload);
    pkg.citations.push_back("citation");
    pkg.metadata["source"] = "test";

    auto decoded = ContextPackageFromJson(ContextPackageToJson(pkg));
    return Check(decoded.messages.size() == 1 && decoded.messages[0].payloadRef == message.payloadRef &&
                      decoded.memoryText == pkg.memoryText && decoded.entities.size() == 1 && decoded.relations.size() == 1 &&
                      decoded.relations[0].metadata == relation.metadata && decoded.entities[0].metadata == entity.metadata &&
                      decoded.payloadRefs.size() == 1 &&

                     decoded.payloadRefs[0].agentId == payload.agentId && decoded.payloadRefs[0].sessionId == payload.sessionId &&
                     decoded.payloadRefs[0].metadata == payload.metadata && decoded.citations == pkg.citations &&
                     decoded.metadata == pkg.metadata,
                 "context package round-trip failed");
}

bool TestRequestRoundTrips()
{
    MemoryContextRequest context;
    context.agentId = "agent";
    context.sessionId = "session";
    context.query = "query";
    context.tokenBudget = 128;
    context.includeSections.push_back("payloads");
    context.metadata["long_term_limit"] = "3";
    auto decodedContext = ContextRequestFromJson(ContextRequestToJson(context));
    if (!Check(decodedContext.includeSections == context.includeSections && decodedContext.metadata == context.metadata,
               "context request round-trip failed")) {
        return false;
    }

    MemoryPayloadWriteRequest payload;
    payload.agentId = "agent";
    payload.sessionId = "session";
    payload.content = "content";
    payload.contentType = "text/plain";
    payload.toolCallId = "call";
    payload.toolName = "tool";
    payload.metadata["k"] = "v";
    auto decodedPayload = PayloadWriteRequestFromJson(PayloadWriteRequestToJson(payload));
    if (!Check(decodedPayload.metadata == payload.metadata && decodedPayload.toolName == payload.toolName,
               "payload request round-trip failed")) {
        return false;
    }

    MemoryConsolidationRequest consolidation;
    consolidation.agentId = "agent";
    consolidation.sessionId = "session";
    consolidation.maxEvents = 7;
    consolidation.forceReprocess = true;
    consolidation.metadata["k"] = "v";
    auto decodedConsolidation = ConsolidationRequestFromJson(ConsolidationRequestToJson(consolidation));
    if (!Check(decodedConsolidation.metadata == consolidation.metadata && decodedConsolidation.forceReprocess,
               "consolidation request round-trip failed")) {
        return false;
    }

    MemorySearchRequest search;
    search.agentId = "agent";
    search.sessionId = "session";
    search.query = "query";
    search.limit = 5;
    search.includeSections.push_back("entities");
    search.metadata["k"] = "v";
    auto decodedSearch = SearchRequestFromJson(SearchRequestToJson(search));
    return Check(decodedSearch.includeSections == search.includeSections && decodedSearch.metadata == search.metadata,
                 "search request round-trip failed");
}

bool TestRelationRoundTrip()
{
    MemoryRelation relation;
    relation.id = "relation";
    relation.fromEntityId = "a";
    relation.relationType = "likes";
    relation.toEntityId = "b";
    relation.confidence = 0.6F;
    relation.sourceRefs.push_back("source");
    relation.metadata["k"] = "v";
    relation.createdAt = "created";
    relation.updatedAt = "updated";

    auto decoded = RelationFromJson(RelationToJson(relation));
    return Check(decoded.id == relation.id && decoded.fromEntityId == relation.fromEntityId &&
                     decoded.relationType == relation.relationType && decoded.toEntityId == relation.toEntityId &&
                     std::fabs(decoded.confidence - relation.confidence) < 0.0001F &&
                     decoded.sourceRefs == relation.sourceRefs && decoded.metadata == relation.metadata &&
                     decoded.createdAt == relation.createdAt && decoded.updatedAt == relation.updatedAt,
                 "relation round-trip failed");
}

bool TestPublicFineGrainedCodecs()
{
    MemoryMessage message;
    message.role = "user";
    message.content = "hello";
    if (!Check(MessageFromJson(MessageToJson(message)).content == message.content, "message codec failed")) {
        return false;
    }

    MemoryPayloadRef payload;
    payload.uri = "file://payload";
    payload.originalChars = 12;
    if (!Check(PayloadRefFromJson(PayloadRefToJson(payload)).originalChars == payload.originalChars,
               "payload ref codec failed")) {
        return false;
    }

    MemoryEntity entity;
    entity.id = "entity";
    entity.confidence = 0.5F;
    return Check(EntityFromJson(EntityToJson(entity)).id == entity.id, "entity codec failed");
}

bool TestSafeMalformedInput()
{
    nlohmann::json eventJson = {{"type", "bad"}, {"agentId", 12}, {"metadata", {{"count", 3}, {"flag", true}}}};
    MemoryEvent event = EventFromJson(eventJson);
    if (!Check(event.type == MemoryEventType::SESSION_STARTED && event.agentId.empty() &&
                    event.metadata["count"] == 3 && event.metadata["flag"] == true,

               "safe event decode failed")) {
        return false;
    }

    nlohmann::json invalidTypeJson = {{"type", 999999}};
    if (!Check(EventFromJson(invalidTypeJson).type == MemoryEventType::SESSION_STARTED,
               "invalid event type fallback failed")) {
        return false;
    }

    nlohmann::json contextJson = {{"tokenBudget", 9223372036854775807LL},
                                  {"includeSections", nlohmann::json::array({"payloads", 7})}};
    MemoryContextRequest context = ContextRequestFromJson(contextJson);
    if (!Check(context.tokenBudget == 4096 && context.includeSections.size() == 1 && context.includeSections[0] == "payloads",
               "safe context decode failed")) {
        return false;
    }

    MemorySearchHit malformedResult;
    malformedResult.id = "id";
    malformedResult.type = "summary";
    malformedResult.content = "content";
    malformedResult.metadata["nested"] = {{"x", 1}};
    nlohmann::json resultJson = SearchResponseToJson({malformedResult});
    auto results = SearchResultsFromJson(resultJson);
    return Check(results.size() == 1 && results[0].score == 0.0F && results[0].metadata["nested"]["x"] == 1,
                 "safe search decode failed");
}

nlohmann::json RandomJson(std::mt19937& rng, int depth)
{
    std::uniform_int_distribution<int> kindDist(0, depth <= 0 ? 4 : 6);
    int kind = kindDist(rng);
    if (kind == 0) return nullptr;
    if (kind == 1) return static_cast<bool>(rng() % 2);
    if (kind == 2) return static_cast<int>(rng());
    if (kind == 3) return "value" + std::to_string(rng() % 100);
    if (kind == 4) return static_cast<double>(rng() % 1000) / 10.0;
    if (kind == 5) {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < 3; ++i) {
            arr.push_back(RandomJson(rng, depth - 1));
        }
        return arr;
    }
    nlohmann::json obj = nlohmann::json::object();
    obj["type"] = RandomJson(rng, depth - 1);
    obj["agentId"] = RandomJson(rng, depth - 1);
    obj["metadata"] = RandomJson(rng, depth - 1);
    obj["messages"] = RandomJson(rng, depth - 1);
    obj["entities"] = RandomJson(rng, depth - 1);
    obj["payloadRefs"] = RandomJson(rng, depth - 1);
    obj["schemaVersion"] = RandomJson(rng, depth - 1);
    return obj;
}

bool TestSchemaAndDiagnostics()
{
    if (!Check(JsonMemorySchemaVersion() == 1 && EventToJson(MemoryEvent()).value("schemaVersion", 0) == 1,
               "schema version missing")) {
        return false;
    }
    auto diagnostics = JsonDecodeDiagnosticsFor({{"type", "bad"}, {"agentId", 7}}, "event");
    if (!Check(diagnostics.ok() && diagnostics.warnings.size() >= 2, "diagnostics warnings missing")) {
        return false;
    }
    MemoryEvent event;
    JsonDecodeDiagnostics decodeDiagnostics;
    if (!Check(DecodeEvent({{"type", "bad"}}, event, &decodeDiagnostics) && !decodeDiagnostics.warnings.empty(),
               "decode diagnostics api failed")) {
        return false;
    }
    MemorySearchRequest searchRequest;
    if (!Check(DecodeSearchRequest({{"limit", "bad"}}, searchRequest, &decodeDiagnostics) &&
                   !decodeDiagnostics.warnings.empty(),
               "search request diagnostics failed")) {
        return false;
    }
     std::vector<MemorySearchHit> results;
     if (!Check(!DecodeSearchHits(nlohmann::json::object(), results, &decodeDiagnostics) &&
                    !decodeDiagnostics.errors.empty(),
                "search results diagnostics failed")) {
         return false;
     }
     auto responseJson = SearchResponseToJson(results);
    if (!Check(responseJson.value("schemaVersion", 0) == 1 && responseJson.value("ok", false) &&
                   responseJson.contains("results"),
               "search response wrapper failed")) {
        return false;
    }
    auto diagnosticsJson = DecodeDiagnosticsToJson(diagnostics);
    return Check(diagnosticsJson.value("schemaVersion", 0) == 1 && diagnosticsJson.contains("warnings"),
                 "diagnostics json failed");
}

bool TestRandomJsonDecoding()
{
    std::mt19937 rng(1234);
    for (int i = 0; i < 200; ++i) {
        auto json = RandomJson(rng, 3);
        EventFromJson(json);
        ContextPackageFromJson(json);
        ContextRequestFromJson(json);
        PayloadWriteRequestFromJson(json);
        PayloadWriteResultFromJson(json);
        ConsolidationRequestFromJson(json);
        ConsolidationResultFromJson(json);
        SearchRequestFromJson(json);
        SearchResultsFromJson(json);
        StatsFromJson(json);
        RelationFromJson(json);
        JsonDecodeDiagnosticsFor(json, "event");
        JsonDecodeDiagnosticsFor(json, "contextPackage");
    }
    return true;
}

bool TestResultRoundTrips()
{
    MemoryPayloadWriteResult payload;
    payload.offloaded = true;
    payload.replacementContent = "replacement";
    payload.payload.uri = "file://payload";
    payload.payload.metadata["k"] = "v";
    auto decodedPayload = PayloadWriteResultFromJson(PayloadWriteResultToJson(payload));
    if (!Check(decodedPayload.offloaded && decodedPayload.payload.metadata == payload.payload.metadata,
               "payload result round-trip failed")) {
        return false;
    }

     std::vector<MemorySearchHit> results;
     MemorySearchHit result;
     result.id = "id";
     result.type = "summary";
     result.content = "content";
     result.score = 0.7F;
     result.sourceRefs.push_back("source");
     result.metadata["k"] = "v";
     results.push_back(result);
     auto decodedResults = SearchResultsFromJson(SearchResponseToJson(results));
    if (!Check(decodedResults.size() == 1 && std::fabs(decodedResults[0].score - result.score) < 0.0001F &&
                   decodedResults[0].sourceRefs == result.sourceRefs && decodedResults[0].metadata == result.metadata,
               "search results round-trip failed")) {
        return false;
    }

    MemoryStats stats;
    stats.events = 1;
    stats.payloads = 2;
    stats.summaries = 3;
    stats.entities = 4;
    stats.relations = 5;
    stats.metadata["source"] = "test";
    auto decodedStats = StatsFromJson(StatsToJson(stats));
    return Check(decodedStats.metadata == stats.metadata && decodedStats.relations == stats.relations,
                 "stats round-trip failed");
}

} // namespace

int main()
{
    if (!TestEventRoundTrip() || !TestContextRoundTrip() || !TestRequestRoundTrips() || !TestRelationRoundTrip() ||
        !TestPublicFineGrainedCodecs() || !TestSafeMalformedInput() || !TestSchemaAndDiagnostics() ||
        !TestRandomJsonDecoding() || !TestResultRoundTrips()) {
        return 1;
    }
    return 0;
}
