#include "context_builder.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>

#include "context_sections.h"
#include "json_helpers.h"
#include "payload_query.h"
#include "store.h"

namespace agent_memory {

namespace {

void AddUniqueCitation(std::vector<std::string>& citations, const std::string& citation)
{
    if (!citation.empty() && std::find(citations.begin(), citations.end(), citation) == citations.end()) {
        citations.push_back(citation);
    }
}

void AddUniqueCitations(std::vector<std::string>& citations, const std::vector<std::string>& refs)
{
    for (const auto& ref : refs) {
        AddUniqueCitation(citations, ref);
    }
}

bool RelationIdMatches(const MemoryRelation& relation, const std::string& resultId)
{
    return relation.id == resultId || ("relation:" + relation.id) == resultId;
}

struct SelectedLongTermMemory
{
    bool succeeded{true};
    LongTermMemorySnapshot snapshot;
    std::vector<MemorySearchResult> searchResults;
    bool usedSearch{false};
    MemoryError error;
};

std::string FormatLongTermSnapshot(const LongTermMemorySnapshot& snapshot)
{
    std::stringstream text;
    if (!snapshot.summaries.empty()) {
        text << "## Long-term Summaries\n";
        for (const auto& summary : snapshot.summaries) {
            text << "- [" << summary.level << "] " << summary.topic << ": " << summary.summary
                 << " (confidence=" << summary.confidence << ")\n";
        }
    }
    if (!snapshot.entities.empty()) {
        if (text.tellp() > 0) {
            text << "\n";
        }
        text << "## Memory Entities\n";
        for (const auto& entity : snapshot.entities) {
            text << "- " << entity.id << " (" << entity.entityType << ", " << entity.name << "): "
                 << entity.summary << " (confidence=" << entity.confidence << ")\n";
        }
    }
    if (!snapshot.relations.empty()) {
        if (text.tellp() > 0) {
            text << "\n";
        }
        text << "## Memory Relations\n";
        for (const auto& relation : snapshot.relations) {
            text << "- " << relation.fromEntityId << " " << relation.relationType << " " << relation.toEntityId
                 << " (confidence=" << relation.confidence << ")\n";
        }
    }
    return text.str();
}

std::string FormatSearchResults(const std::vector<MemorySearchResult>& searchResults)
{
    if (searchResults.empty()) {
        return "";
    }
    std::stringstream stream;
    stream << "## Relevant Long-Term Memory\n";
    for (const auto& item : searchResults) {
        stream << "- [" << item.type << "] " << item.content << "\n";
    }
    return stream.str();
}

void CollectSnapshotCitations(std::vector<std::string>& citations, const LongTermMemorySnapshot& snapshot)
{
    for (const auto& summary : snapshot.summaries) {
        AddUniqueCitations(citations, summary.sourceRefs);
    }
    for (const auto& entity : snapshot.entities) {
        AddUniqueCitations(citations, entity.sourceRefs);
    }
    for (const auto& relation : snapshot.relations) {
        AddUniqueCitations(citations, relation.sourceRefs);
    }
}

void CollectSearchCitations(std::vector<std::string>& citations, const std::vector<MemorySearchResult>& searchResults)
{
    for (const auto& result : searchResults) {
        AddUniqueCitations(citations, result.sourceRefs);
    }
}

SelectedLongTermMemory SelectLongTermMemory(MemoryLongTermStore* longTermStore, MemorySearchStore* searchStore,
                                             const MemoryContextRequest& request, int limit);
std::string RenderLongTermMemory(const SelectedLongTermMemory& selected, std::vector<std::string>& citations);

} // namespace

ContextBuilder::ContextBuilder(const MemoryConfig& config, MemoryEventStore* eventStore, MemoryPayloadStore* payloadStore,
                               MemoryLongTermStore* longTermStore, MemorySearchStore* searchStore)
    : config_(config), eventStore_(eventStore), payloadStore_(payloadStore), longTermStore_(longTermStore), searchStore_(searchStore)
{
}

MemoryContextResult ContextBuilder::BuildContext(const MemoryContextRequest& request) const
{
    MemoryContextPackage result;
    if (ShouldInclude(request, context_sections::Messages)) {
        auto messages = LoadMessagesForContext(request);
        if (!messages) {
            return {false, {}, messages.error};
        }
        for (const auto& event : messages.events) {
            MemoryMessage message;
            message.role = event.role;
            message.content = event.content;
            message.toolCallId = event.toolCallId;
            message.toolName = event.toolName;
            message.payloadRef = event.payloadRef;
            result.messages.push_back(std::move(message));
        }
    }
    result.metadata["message_count"] = result.messages.size();

    int longTermLimit = LongTermMemoryLimit(request);
    std::vector<std::string> citations;
    if (ShouldInclude(request, context_sections::LongTerm) && longTermStore_ != nullptr && searchStore_ != nullptr && longTermLimit > 0) {
        SelectedLongTermMemory selected = SelectLongTermMemory(longTermStore_, searchStore_, request, longTermLimit);
        if (!selected.succeeded) {
            return {false, {}, selected.error};
        }
        result.entities = selected.snapshot.entities;
        result.relations = selected.snapshot.relations;
        std::string longTermText = RenderLongTermMemory(selected, citations);
        if (!longTermText.empty()) {
            if (!result.memoryText.empty()) {
                result.memoryText += "\n\n";
            }
            result.memoryText += longTermText;
        }
    }

    result.metadata["agentId"] = request.agentId;
    result.metadata["sessionId"] = request.sessionId;
    result.metadata["tokenBudget"] = request.tokenBudget > 0 ? request.tokenBudget : config_.tokenBudget;
    result.metadata["long_term_limit"] = longTermLimit;
    result.metadata["include_count"] = request.includeSections.size();
    result.metadata["query_applied"] = !request.query.empty();
    if (!citations.empty()) {
        result.citations = citations;
        result.metadata["citation_count"] = citations.size();
    }
    result.metadata["entity_count"] = result.entities.size();
    result.metadata["relation_count"] = result.relations.size();

    if (ShouldInclude(request, context_sections::Payloads)) {
        auto selectedPayloads = LoadPayloadsForContext(request);
        if (!selectedPayloads) {
            return {false, {}, selectedPayloads.error};
        }
        if (!selectedPayloads.payloads.empty()) {
            std::stringstream payloadOverview;
            if (!result.memoryText.empty()) {
                payloadOverview << "\n";
            }
            payloadOverview << "## Offloaded Payloads\n\n";
            for (const auto& p : selectedPayloads.payloads) {
                payloadOverview << "- uri: " << p.uri
                                << ", tool: " << p.toolName
                                << ", chars: " << p.originalChars
                                << ", summary: " << p.summary << "\n";
            }
            result.metadata["payload_count"] = selectedPayloads.payloads.size();
            result.memoryText += payloadOverview.str();
            result.payloadRefs = std::move(selectedPayloads.payloads);
        } else {
            result.metadata["payload_count"] = 0;
        }
    }
    return {true, result, {}};
}

namespace {

SelectedLongTermMemory SelectLongTermMemory(MemoryLongTermStore* longTermStore, MemorySearchStore* searchStore,
                                             const MemoryContextRequest& request, int limit)
{
    SelectedLongTermMemory selected;
    if (longTermStore == nullptr || searchStore == nullptr || limit <= 0) {
        return selected;
    }

    if (request.query.empty()) {
        auto snapshot = longTermStore->LoadLongTermMemory(request.agentId, limit, request.sessionId);
        if (!snapshot) {
            selected.succeeded = false;
            selected.error = snapshot.error;
            return selected;
        }
        selected.snapshot = std::move(snapshot.snapshot);
        return selected;
    }

    MemorySearchRequest searchRequest;
    searchRequest.agentId = request.agentId;
    searchRequest.sessionId = request.sessionId;
    searchRequest.query = request.query;
    searchRequest.limit = limit;
    auto searchResult = searchStore->SearchLongTermMemory(searchRequest);
    if (!searchResult) {
        selected.succeeded = false;
        selected.error = searchResult.error;
        return selected;
    }
    selected.searchResults = std::move(searchResult.results);
    selected.usedSearch = true;

    auto snapshotResult = longTermStore->LoadLongTermMemory(request.agentId, limit, request.sessionId);
    if (!snapshotResult) {
        selected.succeeded = false;
        selected.error = snapshotResult.error;
        return selected;
    }
    LongTermMemorySnapshot snapshot = std::move(snapshotResult.snapshot);
    std::set<std::string> selectedEntityIds;
    std::set<std::string> selectedRelationIds;
    for (const auto& result : selected.searchResults) {
        if (result.type == "entity") {
            selectedEntityIds.insert(result.id);
        } else if (result.type == "relation") {
            selectedRelationIds.insert(result.id);
        }
    }

    for (const auto& entity : snapshot.entities) {
        if (selectedEntityIds.find(entity.id) != selectedEntityIds.end()) {
            selected.snapshot.entities.push_back(entity);
        }
    }
    for (const auto& relation : snapshot.relations) {
        if (std::any_of(selectedRelationIds.begin(), selectedRelationIds.end(), [&](const std::string& id) {
                return RelationIdMatches(relation, id);
            })) {
            selected.snapshot.relations.push_back(relation);
        }
    }
    return selected;
}

std::string RenderLongTermMemory(const SelectedLongTermMemory& selected,
                                 std::vector<std::string>& citations)
{
    if (selected.usedSearch) {
        CollectSnapshotCitations(citations, selected.snapshot);
        CollectSearchCitations(citations, selected.searchResults);
        return FormatSearchResults(selected.searchResults);
    }

    CollectSnapshotCitations(citations, selected.snapshot);
    return FormatLongTermSnapshot(selected.snapshot);
}

} // namespace

MemoryEventsResult ContextBuilder::LoadMessagesForContext(const MemoryContextRequest& request) const
{
    MemoryEventsResult result;
    if (eventStore_ == nullptr) {
        result.succeeded = true;
        return result;
    }

    int limit = std::max(0, MetadataInt(request.metadata, "message_limit", 20));
    if (limit == 0) {
        result.succeeded = true;
        return result;
    }

    auto events = eventStore_->LoadRecentEvents(request.agentId, request.sessionId, limit);
    if (!events) {
        return events;
    }
    for (const auto& event : events.events) {
        if (event.type == MemoryEventType::MESSAGE_APPENDED) {
            result.events.push_back(event);
        }
    }
    result.succeeded = true;
    return result;
}

MemoryPayloadRefsResult ContextBuilder::LoadPayloadsForContext(const MemoryContextRequest& request) const
{
    MemoryPayloadRefsResult result;
    int limit = std::max(0, MetadataInt(request.metadata, "payload_limit", 20));
    if (limit == 0 || payloadStore_ == nullptr) {
        result.succeeded = true;
        return result;
    }

    auto payloads = payloadStore_->LoadRecentPayloads(request.agentId, request.sessionId, limit);
    if (!payloads) {
        return payloads;
    }

    PayloadQuery query = ParsePayloadQuery(request.query);
    std::set<std::string> seen;
    for (const auto& payload : payloads.payloads) {
        if (!seen.insert(payload.uri).second || !MatchesPayloadQuery(payload, query)) {
            continue;
        }
        result.payloads.push_back(payload);
        if (limit > 0 && static_cast<int>(result.payloads.size()) >= limit) {
            break;
        }
    }

    result.succeeded = true;
    return result;
}

bool ContextBuilder::ShouldInclude(const MemoryContextRequest& request, std::string_view section) const
{
    if (request.includeSections.empty()) {
        return true;
    }
    for (const auto& item : request.includeSections) {
        if (item == section || (section == context_sections::Payloads && item == context_sections::Payload) ||
            (section == context_sections::LongTerm && item == context_sections::LongTermMemory)) {
            return true;
        }
    }
    return false;
}

int ContextBuilder::LongTermMemoryLimit(const MemoryContextRequest& request) const
{
    int configuredLimit = MetadataInt(request.metadata, "long_term_limit", -1);
    if (configuredLimit >= 0) {
        return configuredLimit;
    }

    int tokenBudget = request.tokenBudget > 0 ? request.tokenBudget : config_.tokenBudget;
    if (tokenBudget <= 0) {
        return 20;
    }
    return std::max(1, std::min(50, tokenBudget / 200));
}

} // namespace agent_memory
