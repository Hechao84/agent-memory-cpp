#include "json_memory_codec.h"

#include <utility>

#include "json_helpers.h"

namespace agent_memory {

namespace detail {

constexpr int kSchemaVersion = 1;

enum class FieldType
{
    String,
    Integer,
    Float,
    Boolean,
    Array,
    Object
};

struct FieldRule
{
    const char* name;
    FieldType type;
};

struct FieldRulesView
{
    const FieldRule* data;
    size_t size;

    bool empty() const { return data == nullptr || size == 0; }
    const FieldRule* begin() const { return data; }
    const FieldRule* end() const { return data + size; }
};

template <size_t N>
FieldRulesView RulesView(const FieldRule (&rules)[N])
{
    return {rules, N};
}

nlohmann::json WithSchema(nlohmann::json j)
{
    if (j.is_object()) {
        j["schemaVersion"] = kSchemaVersion;
    }
    return j;
}

nlohmann::json WithEnvelope(bool ok, nlohmann::json data, const MemoryError& error)
{
    nlohmann::json envelope = {{"ok", ok}};
    if (ok) {
        envelope["data"] = std::move(data);
    } else {
        envelope["error"] = ErrorToJson(error);
    }
    return WithSchema(envelope);
}

void AddIssue(std::vector<JsonDecodeIssue>& issues, const std::string& path, const std::string& message)
{
    issues.push_back({path, message});
}

bool MatchesType(const nlohmann::json& value, FieldType type)
{
    switch (type) {
    case FieldType::String:
        return value.is_string();
    case FieldType::Integer:
        return value.is_number_integer() || value.is_number_unsigned();
    case FieldType::Float:
        return value.is_number();
    case FieldType::Boolean:
        return value.is_boolean();
    case FieldType::Array:
        return value.is_array();
    case FieldType::Object:
        return value.is_object();
    }
    return false;
}

std::string FieldTypeName(FieldType type)
{
    switch (type) {
    case FieldType::String:
        return "string";
    case FieldType::Integer:
        return "integer";
    case FieldType::Float:
        return "number";
    case FieldType::Boolean:
        return "boolean";
    case FieldType::Array:
        return "array";
    case FieldType::Object:
        return "object";
    }
    return "unknown";
}

FieldRulesView SchemaRules(const std::string& schemaName)
{
    static constexpr FieldRule message[] = {{"role", FieldType::String}, {"content", FieldType::String},
                                            {"toolCallId", FieldType::String}, {"toolName", FieldType::String},
                                            {"payloadRef", FieldType::String}};
    static constexpr FieldRule payloadRef[] = {{"uri", FieldType::String}, {"contentType", FieldType::String},
                                               {"summary", FieldType::String}, {"toolName", FieldType::String},
                                               {"originalChars", FieldType::Integer}, {"metadata", FieldType::Object},
                                               {"createdAt", FieldType::String}};
    static constexpr FieldRule entity[] = {{"id", FieldType::String}, {"entityType", FieldType::String},
                                           {"name", FieldType::String}, {"summary", FieldType::String},
                                           {"confidence", FieldType::Float}, {"sourceRefs", FieldType::Array},
                                           {"metadata", FieldType::Object}, {"createdAt", FieldType::String},
                                           {"updatedAt", FieldType::String}};
    static constexpr FieldRule relation[] = {{"id", FieldType::String}, {"fromEntityId", FieldType::String},
                                             {"relationType", FieldType::String}, {"toEntityId", FieldType::String},
                                             {"confidence", FieldType::Float}, {"sourceRefs", FieldType::Array},
                                             {"metadata", FieldType::Object}, {"createdAt", FieldType::String},
                                             {"updatedAt", FieldType::String}};
    static constexpr FieldRule event[] = {{"type", FieldType::Integer}, {"agentId", FieldType::String},
                                          {"sessionId", FieldType::String}, {"role", FieldType::String},
                                          {"content", FieldType::String}, {"toolCallId", FieldType::String},
                                          {"toolName", FieldType::String}, {"payloadRef", FieldType::String},
                                          {"metadata", FieldType::Object}, {"timestamp", FieldType::String}};
    static constexpr FieldRule contextRequest[] = {{"agentId", FieldType::String}, {"sessionId", FieldType::String},
                                                   {"query", FieldType::String}, {"tokenBudget", FieldType::Integer},
                                                   {"includeSections", FieldType::Array}, {"metadata", FieldType::Object}};
    static constexpr FieldRule contextPackage[] = {{"messages", FieldType::Array}, {"memoryText", FieldType::String},
                                                   {"entities", FieldType::Array}, {"relations", FieldType::Array},
                                                   {"payloadRefs", FieldType::Array}, {"citations", FieldType::Array},
                                                   {"metadata", FieldType::Object}};
    static constexpr FieldRule payloadWriteRequest[] = {{"agentId", FieldType::String}, {"sessionId", FieldType::String},
                                                        {"content", FieldType::String}, {"contentType", FieldType::String},
                                                        {"toolCallId", FieldType::String}, {"toolName", FieldType::String},
                                                        {"metadata", FieldType::Object}};
    static constexpr FieldRule payloadWriteResult[] = {{"offloaded", FieldType::Boolean},
                                                       {"replacementContent", FieldType::String},
                                                       {"payload", FieldType::Object}};
    static constexpr FieldRule consolidationRequest[] = {{"agentId", FieldType::String}, {"sessionId", FieldType::String},
                                                         {"maxEvents", FieldType::Integer}, {"forceReprocess", FieldType::Boolean},
                                                         {"metadata", FieldType::Object}};
    static constexpr FieldRule consolidationResult[] = {{"succeeded", FieldType::Boolean}, {"fallbackUsed", FieldType::Boolean},
                                                        {"processedEvents", FieldType::Integer},
                                                        {"savedSummaries", FieldType::Integer},
                                                        {"savedEntities", FieldType::Integer},
                                                        {"savedRelations", FieldType::Integer},
                                                        {"nextCursor", FieldType::String}, {"sessionId", FieldType::String},
                                                        {"error", FieldType::String}};
    static constexpr FieldRule searchRequest[] = {{"agentId", FieldType::String}, {"sessionId", FieldType::String},
                                                  {"query", FieldType::String}, {"limit", FieldType::Integer},
                                                  {"includeSections", FieldType::Array}, {"metadata", FieldType::Object}};
    static constexpr FieldRule searchResponse[] = {{"ok", FieldType::Boolean}, {"results", FieldType::Array}};
    static constexpr FieldRule stats[] = {{"events", FieldType::Integer}, {"payloads", FieldType::Integer},
                                          {"summaries", FieldType::Integer}, {"entities", FieldType::Integer},
                                          {"relations", FieldType::Integer}, {"metadata", FieldType::Object}};

    if (schemaName == "message") return RulesView(message);
    if (schemaName == "payloadRef") return RulesView(payloadRef);
    if (schemaName == "entity") return RulesView(entity);
    if (schemaName == "relation") return RulesView(relation);
    if (schemaName == "event") return RulesView(event);
    if (schemaName == "contextRequest") return RulesView(contextRequest);
    if (schemaName == "contextPackage") return RulesView(contextPackage);
    if (schemaName == "payloadWriteRequest") return RulesView(payloadWriteRequest);
    if (schemaName == "payloadWriteResult") return RulesView(payloadWriteResult);
    if (schemaName == "consolidationRequest") return RulesView(consolidationRequest);
    if (schemaName == "consolidationResult") return RulesView(consolidationResult);
    if (schemaName == "searchRequest") return RulesView(searchRequest);
    if (schemaName == "searchResponse") return RulesView(searchResponse);
    if (schemaName == "stats") return RulesView(stats);
    return {nullptr, 0};
}

MemoryEventType EventTypeFromJson(const nlohmann::json& j)
{
    const int type = JsonInt(j, "type", static_cast<int>(MemoryEventType::SESSION_STARTED));
    switch (static_cast<MemoryEventType>(type)) {
    case MemoryEventType::SESSION_STARTED:
    case MemoryEventType::SESSION_ENDED:
    case MemoryEventType::MESSAGE_APPENDED:
    case MemoryEventType::TOOL_CALL_STARTED:
    case MemoryEventType::TOOL_CALL_FINISHED:
    case MemoryEventType::PAYLOAD_OFFLOADED:
    case MemoryEventType::CONSOLIDATION_REQUESTED:
    case MemoryEventType::CONSOLIDATION_COMPLETED:
        return static_cast<MemoryEventType>(type);
    }
    return MemoryEventType::SESSION_STARTED;
}

nlohmann::json SearchResultsArrayToJson(const std::vector<MemorySearchResult>& results)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& result : results) {
        arr.push_back({{"id", result.id},
                       {"type", result.type},
                       {"content", result.content},
                       {"score", result.score},
                       {"sourceRefs", result.sourceRefs},
                       {"metadata", JsonObject(result.metadata)}});
    }
    return arr;
}

template <typename T, typename F>
bool DecodeWithDiagnostics(const nlohmann::json& j, T& value, JsonDecodeDiagnostics* diagnostics,
                           const std::string& schemaName, F&& decode)
{
    if (diagnostics != nullptr) {
        *diagnostics = JsonDecodeDiagnosticsFor(j, schemaName);
    }
    value = decode(j);
    return diagnostics == nullptr || diagnostics->ok();
}

} // namespace detail

using detail::WithSchema;
using detail::kSchemaVersion;
using detail::EventTypeFromJson;
using detail::SearchResultsArrayToJson;
using detail::DecodeWithDiagnostics;
using detail::AddIssue;
using detail::MatchesType;
using detail::FieldTypeName;
using detail::SchemaRules;

int JsonMemorySchemaVersion()
{
    return kSchemaVersion;
}

nlohmann::json DecodeDiagnosticsToJson(const JsonDecodeDiagnostics& diagnostics)
{
    auto issuesToJson = [](const std::vector<JsonDecodeIssue>& issues) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& issue : issues) {
            arr.push_back({{"path", issue.path}, {"message", issue.message}});
        }
        return arr;
    };
    return WithSchema({{"ok", diagnostics.ok()},
                       {"warnings", issuesToJson(diagnostics.warnings)},
                       {"errors", issuesToJson(diagnostics.errors)}});
}

JsonDecodeDiagnostics JsonDecodeDiagnosticsFor(const nlohmann::json& j, const std::string& schemaName)
{
    JsonDecodeDiagnostics diagnostics;
    if (!j.is_object()) {
        AddIssue(diagnostics.errors, "$", std::string("expected object, got ") + j.type_name());
        return diagnostics;
    }
    if (j.contains("schemaVersion") && !j["schemaVersion"].is_number_integer()) {
        AddIssue(diagnostics.warnings, "$.schemaVersion", std::string("expected integer, got ") + j["schemaVersion"].type_name());
    } else if (JsonInt(j, "schemaVersion", kSchemaVersion) > kSchemaVersion) {
        AddIssue(diagnostics.warnings, "$.schemaVersion", "newer schema version");
    }
    auto rules = SchemaRules(schemaName);
    if (rules.empty() && !schemaName.empty()) {
        AddIssue(diagnostics.warnings, "$", "unknown schema name: " + schemaName);
        return diagnostics;
    }
    for (const auto& rule : rules) {
        if (!j.contains(rule.name) || MatchesType(j[rule.name], rule.type)) {
            continue;
        }
        AddIssue(diagnostics.warnings, std::string("$.") + rule.name,
                 "expected " + FieldTypeName(rule.type) + ", got " + j[rule.name].type_name());
    }
    if (schemaName == "searchResponse" && !JsonFieldValue(j, "results").is_array()) {
        AddIssue(diagnostics.errors, "$.results", "expected array");
    }
    return diagnostics;
}

nlohmann::json ErrorToJson(const MemoryError& error)
{
    if (!error) {
        return nlohmann::json::object();
    }
    return {{"code", error.code}, {"message", error.message}, {"details", error.details}, {"retryable", error.retryable}};
}

MemoryError ErrorFromJson(const nlohmann::json& j)
{
    MemoryError error;
    if (!j.is_object()) {
        return error;
    }
    error.code = JsonString(j, "code");
    error.message = JsonString(j, "message");
    error.details = JsonString(j, "details");
    error.retryable = JsonBool(j, "retryable", false);
    return error;
}

nlohmann::json OperationResultToJson(const MemoryOperationResult& result)
{
    return WithSchema({{"succeeded", result.succeeded}, {"error", ErrorToJson(result.error)}});
}

nlohmann::json SuccessEnvelope(nlohmann::json data)
{
    return detail::WithEnvelope(true, std::move(data), {});
}

nlohmann::json ErrorEnvelope(const MemoryError& error)
{
    return detail::WithEnvelope(false, nlohmann::json::object(), error);
}

nlohmann::json MessageToJson(const MemoryMessage& message)
{
    return {{"role", message.role},
            {"content", message.content},
            {"toolCallId", message.toolCallId},
            {"toolName", message.toolName},
            {"payloadRef", message.payloadRef}};
}

MemoryMessage MessageFromJson(const nlohmann::json& j)
{
    MemoryMessage message;
    message.role = JsonString(j, "role");
    message.content = JsonString(j, "content");
    message.toolCallId = JsonString(j, "toolCallId");
    message.toolName = JsonString(j, "toolName");
    message.payloadRef = JsonString(j, "payloadRef");
    return message;
}

nlohmann::json PayloadRefToJson(const MemoryPayloadRef& payload)
{
    return {{"agentId", payload.agentId},
            {"sessionId", payload.sessionId},
            {"uri", payload.uri},
            {"contentType", payload.contentType},
            {"summary", payload.summary},
            {"toolName", payload.toolName},
            {"originalChars", payload.originalChars},
            {"metadata", JsonObject(payload.metadata)},
            {"createdAt", payload.createdAt}};
}

MemoryPayloadRef PayloadRefFromJson(const nlohmann::json& j)
{
    MemoryPayloadRef payload;
    payload.agentId = JsonString(j, "agentId");
    payload.sessionId = JsonString(j, "sessionId");
    payload.uri = JsonString(j, "uri");
    payload.contentType = JsonString(j, "contentType");
    payload.summary = JsonString(j, "summary");
    payload.toolName = JsonString(j, "toolName");
    payload.originalChars = JsonInt(j, "originalChars", 0);
    payload.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    payload.createdAt = JsonString(j, "createdAt");
    return payload;
}

nlohmann::json EntityToJson(const MemoryEntity& entity)
{
    return {{"id", entity.id},
            {"entityType", entity.entityType},
            {"name", entity.name},
            {"summary", entity.summary},
            {"confidence", entity.confidence},
            {"sourceRefs", entity.sourceRefs},
            {"metadata", JsonObject(entity.metadata)},
            {"createdAt", entity.createdAt},
            {"updatedAt", entity.updatedAt}};
}

MemoryEntity EntityFromJson(const nlohmann::json& j)
{
    MemoryEntity entity;
    entity.id = JsonString(j, "id");
    entity.entityType = JsonString(j, "entityType");
    entity.name = JsonString(j, "name");
    entity.summary = JsonString(j, "summary");
    entity.confidence = JsonFloat(j, "confidence", 0.0F);
    entity.sourceRefs = StringVectorFromJson(JsonFieldValue(j, "sourceRefs"));
    entity.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    entity.createdAt = JsonString(j, "createdAt");
    entity.updatedAt = JsonString(j, "updatedAt");
    return entity;
}

nlohmann::json RelationToJson(const MemoryRelation& relation)
{
    return {{"id", relation.id},
            {"fromEntityId", relation.fromEntityId},
            {"relationType", relation.relationType},
            {"toEntityId", relation.toEntityId},
            {"confidence", relation.confidence},
            {"sourceRefs", relation.sourceRefs},
            {"metadata", JsonObject(relation.metadata)},
            {"createdAt", relation.createdAt},
            {"updatedAt", relation.updatedAt}};
}

MemoryRelation RelationFromJson(const nlohmann::json& j)
{
    MemoryRelation relation;
    relation.id = JsonString(j, "id");
    relation.fromEntityId = JsonString(j, "fromEntityId");
    relation.relationType = JsonString(j, "relationType");
    relation.toEntityId = JsonString(j, "toEntityId");
    relation.confidence = JsonFloat(j, "confidence", 0.0F);
    relation.sourceRefs = StringVectorFromJson(JsonFieldValue(j, "sourceRefs"));
    relation.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    relation.createdAt = JsonString(j, "createdAt");
    relation.updatedAt = JsonString(j, "updatedAt");
    return relation;
}

nlohmann::json StatsToJson(const MemoryStats& stats)
{
    return WithSchema({{"events", stats.events},
                       {"payloads", stats.payloads},
                       {"summaries", stats.summaries},
                       {"entities", stats.entities},
                       {"relations", stats.relations},
                       {"metadata", JsonObject(stats.metadata)}});
}

MemoryStats StatsFromJson(const nlohmann::json& j)
{
    MemoryStats stats;
    stats.events = JsonInt(j, "events", 0);
    stats.payloads = JsonInt(j, "payloads", 0);
    stats.summaries = JsonInt(j, "summaries", 0);
    stats.entities = JsonInt(j, "entities", 0);
    stats.relations = JsonInt(j, "relations", 0);
    stats.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    return stats;
}

nlohmann::json ContextPackageToJson(const MemoryContextPackage& pkg)
{
    nlohmann::json messages = nlohmann::json::array();
    for (const auto& message : pkg.messages) {
        messages.push_back(MessageToJson(message));
    }
    nlohmann::json entities = nlohmann::json::array();
    for (const auto& entity : pkg.entities) {
        entities.push_back(EntityToJson(entity));
    }
    nlohmann::json relations = nlohmann::json::array();
    for (const auto& relation : pkg.relations) {
        relations.push_back(RelationToJson(relation));
    }
    nlohmann::json payloadRefs = nlohmann::json::array();
    for (const auto& payload : pkg.payloadRefs) {
        payloadRefs.push_back(PayloadRefToJson(payload));
    }
    return WithSchema({{"messages", messages},
                       {"memoryText", pkg.memoryText},
                       {"entities", entities},
                       {"relations", relations},
                       {"payloadRefs", payloadRefs},
                       {"citations", pkg.citations},
                       {"metadata", JsonObject(pkg.metadata)}});
}

MemoryContextPackage ContextPackageFromJson(const nlohmann::json& j)
{
    MemoryContextPackage pkg;
    pkg.memoryText = JsonString(j, "memoryText");
    pkg.citations = StringVectorFromJson(JsonFieldValue(j, "citations"));
    pkg.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    const auto& messages = JsonFieldValue(j, "messages");
    if (messages.is_array()) {
        for (const auto& item : messages) {
            if (item.is_object()) pkg.messages.push_back(MessageFromJson(item));
        }
    }
    const auto& entities = JsonFieldValue(j, "entities");
    if (entities.is_array()) {
        for (const auto& item : entities) {
            if (item.is_object()) pkg.entities.push_back(EntityFromJson(item));
        }
    }
    const auto& relations = JsonFieldValue(j, "relations");
    if (relations.is_array()) {
        for (const auto& item : relations) {
            if (item.is_object()) pkg.relations.push_back(RelationFromJson(item));
        }
    }
    const auto& payloadRefs = JsonFieldValue(j, "payloadRefs");
    if (payloadRefs.is_array()) {
        for (const auto& item : payloadRefs) {
            if (item.is_object()) pkg.payloadRefs.push_back(PayloadRefFromJson(item));
        }
    }
    return pkg;
}

nlohmann::json ContextRequestToJson(const MemoryContextRequest& request)
{
    return WithSchema({{"agentId", request.agentId},
                       {"sessionId", request.sessionId},
                       {"query", request.query},
                       {"tokenBudget", request.tokenBudget},
                       {"includeSections", request.includeSections},
                       {"metadata", JsonObject(request.metadata)}});
}

MemoryContextRequest ContextRequestFromJson(const nlohmann::json& j)
{
    MemoryContextRequest request;
    request.agentId = JsonString(j, "agentId");
    request.sessionId = JsonString(j, "sessionId");
    request.query = JsonString(j, "query");
    request.tokenBudget = JsonInt(j, "tokenBudget", 4096);
    request.includeSections = StringVectorFromJson(JsonFieldValue(j, "includeSections"));
    request.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    return request;
}

nlohmann::json EventToJson(const MemoryEvent& event)
{
    return WithSchema({{"type", static_cast<int>(event.type)},
                       {"agentId", event.agentId},
                       {"sessionId", event.sessionId},
                       {"role", event.role},
                       {"content", event.content},
                       {"toolCallId", event.toolCallId},
                       {"toolName", event.toolName},
                       {"payloadRef", event.payloadRef},
                       {"metadata", JsonObject(event.metadata)},
                       {"timestamp", event.timestamp}});
}

MemoryEvent EventFromJson(const nlohmann::json& j)
{
    MemoryEvent event;
    event.type = EventTypeFromJson(j);
    event.agentId = JsonString(j, "agentId");
    event.sessionId = JsonString(j, "sessionId");
    event.role = JsonString(j, "role");
    event.content = JsonString(j, "content");
    event.toolCallId = JsonString(j, "toolCallId");
    event.toolName = JsonString(j, "toolName");
    event.payloadRef = JsonString(j, "payloadRef");
    event.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    event.timestamp = JsonString(j, "timestamp");
    return event;
}

nlohmann::json PayloadWriteRequestToJson(const MemoryPayloadWriteRequest& request)
{
    return WithSchema({{"agentId", request.agentId},
                       {"sessionId", request.sessionId},
                       {"content", request.content},
                       {"contentType", request.contentType},
                       {"toolCallId", request.toolCallId},
                       {"toolName", request.toolName},
                       {"metadata", JsonObject(request.metadata)}});
}

MemoryPayloadWriteRequest PayloadWriteRequestFromJson(const nlohmann::json& j)
{
    MemoryPayloadWriteRequest request;
    request.agentId = JsonString(j, "agentId");
    request.sessionId = JsonString(j, "sessionId");
    request.content = JsonString(j, "content");
    request.contentType = JsonString(j, "contentType");
    request.toolCallId = JsonString(j, "toolCallId");
    request.toolName = JsonString(j, "toolName");
    request.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    return request;
}

nlohmann::json PayloadWriteResultToJson(const MemoryPayloadWriteResult& result)
{
    return WithSchema({{"succeeded", result.succeeded},
                       {"offloaded", result.offloaded},
                       {"replacementContent", result.replacementContent},
                       {"payload", PayloadRefToJson(result.payload)},
                       {"error", ErrorToJson(result.error)}});
}

MemoryPayloadWriteResult PayloadWriteResultFromJson(const nlohmann::json& j, const std::string& defaultReplacementContent)
{
    MemoryPayloadWriteResult result;
    result.succeeded = JsonBool(j, "succeeded", false);
    result.offloaded = JsonBool(j, "offloaded", false);
    result.replacementContent = JsonString(j, "replacementContent", defaultReplacementContent);
    if (JsonFieldValue(j, "payload").is_object()) {
        result.payload = PayloadRefFromJson(j["payload"]);
    }
    result.error = ErrorFromJson(JsonFieldValue(j, "error"));
    return result;
}

nlohmann::json ConsolidationRequestToJson(const MemoryConsolidationRequest& request)
{
    return WithSchema({{"agentId", request.agentId},
                       {"sessionId", request.sessionId},
                       {"maxEvents", request.maxEvents},
                       {"forceReprocess", request.forceReprocess},
                       {"metadata", JsonObject(request.metadata)}});
}

MemoryConsolidationRequest ConsolidationRequestFromJson(const nlohmann::json& j)
{
    MemoryConsolidationRequest request;
    request.agentId = JsonString(j, "agentId");
    request.sessionId = JsonString(j, "sessionId");
    request.maxEvents = JsonInt(j, "maxEvents", 100);
    request.forceReprocess = JsonBool(j, "forceReprocess", false);
    request.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    return request;
}

nlohmann::json ConsolidationResultToJson(const MemoryConsolidationResult& result)
{
    return WithSchema({{"succeeded", result.succeeded},
                       {"fallbackUsed", result.fallbackUsed},
                       {"processedEvents", result.processedEvents},
                       {"savedSummaries", result.savedSummaries},
                       {"savedEntities", result.savedEntities},
                       {"savedRelations", result.savedRelations},
                       {"nextCursor", result.nextCursor},
                       {"sessionId", result.sessionId},
                       {"error", ErrorToJson(result.error)}});
}

MemoryConsolidationResult ConsolidationResultFromJson(const nlohmann::json& j)
{
    MemoryConsolidationResult result;
    result.succeeded = JsonBool(j, "succeeded", false);
    result.fallbackUsed = JsonBool(j, "fallbackUsed", false);
    result.processedEvents = JsonInt(j, "processedEvents", 0);
    result.savedSummaries = JsonInt(j, "savedSummaries", 0);
    result.savedEntities = JsonInt(j, "savedEntities", 0);
    result.savedRelations = JsonInt(j, "savedRelations", 0);
    result.nextCursor = JsonString(j, "nextCursor");
    result.sessionId = JsonString(j, "sessionId");
    result.error = ErrorFromJson(JsonFieldValue(j, "error"));
    return result;
}

nlohmann::json SearchRequestToJson(const MemorySearchRequest& request)
{
    return WithSchema({{"agentId", request.agentId},
                       {"sessionId", request.sessionId},
                       {"query", request.query},
                       {"limit", request.limit},
                       {"includeSections", request.includeSections},
                       {"metadata", JsonObject(request.metadata)}});
}

MemorySearchRequest SearchRequestFromJson(const nlohmann::json& j)
{
    MemorySearchRequest request;
    request.agentId = JsonString(j, "agentId");
    request.sessionId = JsonString(j, "sessionId");
    request.query = JsonString(j, "query");
    request.limit = JsonInt(j, "limit", 10);
    request.includeSections = StringVectorFromJson(JsonFieldValue(j, "includeSections"));
    request.metadata = JsonObject(JsonFieldValue(j, "metadata"));
    return request;
}

nlohmann::json SearchResponseToJson(const std::vector<MemorySearchResult>& results, bool ok)
{
    return WithSchema({{"ok", ok}, {"results", SearchResultsArrayToJson(results)}});
}

std::vector<MemorySearchResult> SearchResultsFromJson(const nlohmann::json& j)
{
    std::vector<MemorySearchResult> results;
    const auto& array = JsonFieldValue(j, "results");
    if (!j.is_object() || !array.is_array()) {
        return results;
    }
    results.reserve(array.size());
    for (const auto& item : array) {
        if (!item.is_object()) {
            continue;
        }
        MemorySearchResult result;
        result.id = JsonString(item, "id");
        result.type = JsonString(item, "type");
        result.content = JsonString(item, "content");
        result.score = JsonFloat(item, "score", 0.0F);
        result.sourceRefs = StringVectorFromJson(JsonFieldValue(item, "sourceRefs"));
        result.metadata = JsonObject(JsonFieldValue(item, "metadata"));
        results.push_back(std::move(result));
    }
    return results;
}

bool DecodeMessage(const nlohmann::json& j, MemoryMessage& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "message", MessageFromJson);
}

bool DecodePayloadRef(const nlohmann::json& j, MemoryPayloadRef& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "payloadRef", PayloadRefFromJson);
}

bool DecodeEntity(const nlohmann::json& j, MemoryEntity& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "entity", EntityFromJson);
}

bool DecodeRelation(const nlohmann::json& j, MemoryRelation& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "relation", RelationFromJson);
}

bool DecodeEvent(const nlohmann::json& j, MemoryEvent& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "event", EventFromJson);
}

bool DecodeContextPackage(const nlohmann::json& j, MemoryContextPackage& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "contextPackage", ContextPackageFromJson);
}

bool DecodeContextRequest(const nlohmann::json& j, MemoryContextRequest& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "contextRequest", ContextRequestFromJson);
}

bool DecodePayloadWriteRequest(const nlohmann::json& j, MemoryPayloadWriteRequest& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "payloadWriteRequest", PayloadWriteRequestFromJson);
}

bool DecodePayloadWriteResult(const nlohmann::json& j, MemoryPayloadWriteResult& value, JsonDecodeDiagnostics* diagnostics)
{
    if (diagnostics != nullptr) {
        *diagnostics = JsonDecodeDiagnosticsFor(j, "payloadWriteResult");
    }
    value = PayloadWriteResultFromJson(j);
    return diagnostics == nullptr || diagnostics->ok();
}

bool DecodeConsolidationRequest(const nlohmann::json& j, MemoryConsolidationRequest& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "consolidationRequest", ConsolidationRequestFromJson);
}

bool DecodeConsolidationResult(const nlohmann::json& j, MemoryConsolidationResult& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "consolidationResult", ConsolidationResultFromJson);
}

bool DecodeSearchRequest(const nlohmann::json& j, MemorySearchRequest& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "searchRequest", SearchRequestFromJson);
}

bool DecodeSearchResults(const nlohmann::json& j, std::vector<MemorySearchResult>& value, JsonDecodeDiagnostics* diagnostics)
{
    if (diagnostics != nullptr) {
        *diagnostics = JsonDecodeDiagnosticsFor(j, "searchResponse");
    }
    value = SearchResultsFromJson(j);
    return diagnostics == nullptr || diagnostics->ok();
}

bool DecodeStats(const nlohmann::json& j, MemoryStats& value, JsonDecodeDiagnostics* diagnostics)
{
    return DecodeWithDiagnostics(j, value, diagnostics, "stats", StatsFromJson);
}

} // namespace agent_memory