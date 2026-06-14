#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/context.h"
#include "agent_memory/event.h"
#include "agent_memory/long_term_memory.h"
#include "agent_memory/payload.h"
#include "agent_memory/search.h"
#include "agent_memory/stats.h"

namespace agent_memory {

struct JsonDecodeIssue
{
    std::string path;
    std::string message;
};

struct JsonDecodeDiagnostics
{
    std::vector<JsonDecodeIssue> warnings;
    std::vector<JsonDecodeIssue> errors;

    bool ok() const { return errors.empty(); }
};

int JsonMemorySchemaVersion();
nlohmann::json DecodeDiagnosticsToJson(const JsonDecodeDiagnostics& diagnostics);
JsonDecodeDiagnostics JsonDecodeDiagnosticsFor(const nlohmann::json& j, const std::string& schemaName);

nlohmann::json ErrorToJson(const MemoryError& error);
nlohmann::json OperationResultToJson(const MemoryOperationResult& result);
nlohmann::json SuccessEnvelope(nlohmann::json data);
nlohmann::json ErrorEnvelope(const MemoryError& error);
MemoryError ErrorFromJson(const nlohmann::json& j);

nlohmann::json MessageToJson(const MemoryMessage& message);
nlohmann::json PayloadRefToJson(const MemoryPayloadRef& payload);
nlohmann::json EntityToJson(const MemoryEntity& entity);
nlohmann::json RelationToJson(const MemoryRelation& relation);
nlohmann::json StatsToJson(const MemoryStats& stats);
nlohmann::json ContextPackageToJson(const MemoryContextPackage& pkg);
nlohmann::json ContextRequestToJson(const MemoryContextRequest& request);
nlohmann::json EventToJson(const MemoryEvent& event);
nlohmann::json PayloadWriteRequestToJson(const MemoryPayloadWriteRequest& request);
nlohmann::json PayloadWriteResultToJson(const MemoryPayloadWriteResult& result);
nlohmann::json ConsolidationRequestToJson(const MemoryConsolidationRequest& request);
nlohmann::json ConsolidationResultToJson(const MemoryConsolidationResult& result);
nlohmann::json SearchRequestToJson(const MemorySearchRequest& request);
nlohmann::json SearchResponseToJson(const std::vector<MemorySearchResult>& results, bool ok = true);

bool DecodeMessage(const nlohmann::json& j, MemoryMessage& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodePayloadRef(const nlohmann::json& j, MemoryPayloadRef& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeEntity(const nlohmann::json& j, MemoryEntity& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeRelation(const nlohmann::json& j, MemoryRelation& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeEvent(const nlohmann::json& j, MemoryEvent& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeContextPackage(const nlohmann::json& j, MemoryContextPackage& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeContextRequest(const nlohmann::json& j, MemoryContextRequest& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodePayloadWriteRequest(const nlohmann::json& j, MemoryPayloadWriteRequest& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodePayloadWriteResult(const nlohmann::json& j, MemoryPayloadWriteResult& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeConsolidationRequest(const nlohmann::json& j, MemoryConsolidationRequest& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeConsolidationResult(const nlohmann::json& j, MemoryConsolidationResult& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeSearchRequest(const nlohmann::json& j, MemorySearchRequest& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeSearchResults(const nlohmann::json& j, std::vector<MemorySearchResult>& value, JsonDecodeDiagnostics* diagnostics = nullptr);
bool DecodeStats(const nlohmann::json& j, MemoryStats& value, JsonDecodeDiagnostics* diagnostics = nullptr);

MemoryMessage MessageFromJson(const nlohmann::json& j);
MemoryPayloadRef PayloadRefFromJson(const nlohmann::json& j);
MemoryEntity EntityFromJson(const nlohmann::json& j);
MemoryRelation RelationFromJson(const nlohmann::json& j);
MemoryEvent EventFromJson(const nlohmann::json& j);
MemoryContextPackage ContextPackageFromJson(const nlohmann::json& j);
MemoryContextRequest ContextRequestFromJson(const nlohmann::json& j);
MemoryPayloadWriteRequest PayloadWriteRequestFromJson(const nlohmann::json& j);
MemoryPayloadWriteResult PayloadWriteResultFromJson(const nlohmann::json& j, const std::string& defaultReplacementContent = "");
MemoryConsolidationRequest ConsolidationRequestFromJson(const nlohmann::json& j);
MemoryConsolidationResult ConsolidationResultFromJson(const nlohmann::json& j);
MemorySearchRequest SearchRequestFromJson(const nlohmann::json& j);
std::vector<MemorySearchResult> SearchResultsFromJson(const nlohmann::json& j);
MemoryStats StatsFromJson(const nlohmann::json& j);

} // namespace agent_memory
