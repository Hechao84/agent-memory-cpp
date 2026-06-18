#include "llm_processor.h"

#include <sstream>

#include <nlohmann/json.hpp>

namespace agent_memory {

LlmLongTermMemoryProcessor::LlmLongTermMemoryProcessor(MemoryModelClient* model)
    : model_(model)
{
}

std::string LlmLongTermMemoryProcessor::BuildPrompt(const LongTermMemoryBatch& batch) const
{
    std::stringstream prompt;
    prompt << R"(Extract structured long-term memory from the conversation events below.
Return ONLY valid JSON with this exact schema (no markdown fences, no explanation):

{
  "topicSummaries": ["brief topic summary 1", "brief topic summary 2"],
  "profileSummaries": ["user profile insight 1"],
  "entities": [
    {
      "id": "entity:unique.id.here",
      "entityType": "preference|topic|project|task|file|user|style",
      "name": "short name",
      "summary": "one-line summary",
      "confidence": 0.0-1.0,
      "sourceRefs": ["session://session-id#event:1"]
    }
  ],
  "relations": [
    {
      "fromEntityId": "entity:user",
      "relationType": "prefers|works_on|mentions|related_to|supersedes|contradicts",
      "toEntityId": "entity:...",
      "confidence": 0.0-1.0,
      "sourceRefs": ["session://session-id#event:1"]
    }
  ]
}

Use "supersedes" when new information replaces an older entity.
Use "contradicts" when information conflicts with a previous entity.
Use "prefers" for user preferences.

Conversation events:
)";

    for (size_t i = 0; i < batch.events.size(); ++i) {
        const auto& event = batch.events[i];
        std::string sourceRef = i < batch.sourceRefs.size() ? batch.sourceRefs[i] : "event://" + std::to_string(i + 1);
        prompt << sourceRef << " [" << event.role << "]: " << event.content << "\n";
    }

    return prompt.str();
}

LongTermMemoryUpdate LlmLongTermMemoryProcessor::ParseResponse(const std::string& jsonStr,
                                                                const std::vector<std::string>& sourceRefs,
                                                                const std::string& agentId) const
{
    LongTermMemoryUpdate update;
    auto json = nlohmann::json::parse(jsonStr, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return update;
    }

    if (json.contains("topicSummaries") && json["topicSummaries"].is_array()) {
        for (const auto& item : json["topicSummaries"]) {
            if (item.is_string()) {
                update.topicSummaries.push_back(item.get<std::string>());
            }
        }
    }
    if (json.contains("profileSummaries") && json["profileSummaries"].is_array()) {
        for (const auto& item : json["profileSummaries"]) {
            if (item.is_string()) {
                update.profileSummaries.push_back(item.get<std::string>());
            }
        }
    }
    if (json.contains("entities") && json["entities"].is_array()) {
        for (const auto& item : json["entities"]) {
            if (!item.is_object()) {
                continue;
            }
            MemoryEntity entity;
            entity.id = item.contains("id") && item["id"].is_string()
                ? item["id"].get<std::string>()
                : "entity:llm." + std::to_string(update.entities.size());
            entity.agentId = agentId;
            entity.entityType = item.contains("entityType") && item["entityType"].is_string() ? item["entityType"].get<std::string>() : "unknown";
            entity.name = item.contains("name") && item["name"].is_string() ? item["name"].get<std::string>() : "";
            entity.summary = item.contains("summary") && item["summary"].is_string() ? item["summary"].get<std::string>() : "";
            entity.confidence = item.contains("confidence") && item["confidence"].is_number() ? item["confidence"].get<float>() : 0.5F;
            if (item.contains("sourceRefs") && item["sourceRefs"].is_array()) {
                for (const auto& ref : item["sourceRefs"]) {
                    if (ref.is_string()) {
                        entity.sourceRefs.push_back(ref.get<std::string>());
                    }
                }
            }
            if (entity.sourceRefs.empty()) {
                entity.sourceRefs = sourceRefs;
            }
            update.entities.push_back(std::move(entity));
        }
    }
    if (json.contains("relations") && json["relations"].is_array()) {
        for (const auto& item : json["relations"]) {
            if (!item.is_object()) {
                continue;
            }
            MemoryRelation relation;
            relation.agentId = agentId;
            relation.fromEntityId = item.contains("fromEntityId") && item["fromEntityId"].is_string() ? item["fromEntityId"].get<std::string>() : "";
            relation.relationType = item.contains("relationType") && item["relationType"].is_string() ? item["relationType"].get<std::string>() : "related_to";
            relation.toEntityId = item.contains("toEntityId") && item["toEntityId"].is_string() ? item["toEntityId"].get<std::string>() : "";
            relation.confidence = item.contains("confidence") && item["confidence"].is_number() ? item["confidence"].get<float>() : 0.5F;
            if (item.contains("sourceRefs") && item["sourceRefs"].is_array()) {
                for (const auto& ref : item["sourceRefs"]) {
                    if (ref.is_string()) {
                        relation.sourceRefs.push_back(ref.get<std::string>());
                    }
                }
            }
            if (relation.sourceRefs.empty()) {
                relation.sourceRefs = sourceRefs;
            }
            update.relations.push_back(std::move(relation));
        }
    }

    return update;
}

LongTermMemoryUpdate LlmLongTermMemoryProcessor::Process(const LongTermMemoryBatch& batch)
{
    if (!model_) {
        return {};
    }

    ModelInvokeResult invokeResult = model_->GenerateMemoryUpdate(BuildPrompt(batch));
    if (!invokeResult) {
        return {};
    }

    std::string cleaned = invokeResult.text;
    size_t start = cleaned.find('{');
    size_t end = cleaned.rfind('}');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        cleaned = cleaned.substr(start, end - start + 1);
    }

    return ParseResponse(cleaned, batch.sourceRefs, batch.events.empty() ? "" : batch.events[0].agentId);
}

} // namespace agent_memory
