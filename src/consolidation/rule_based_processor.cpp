#include "rule_based_processor.h"

#include <set>
#include <utility>

#include "string_util.h"

namespace agent_memory {

bool RuleBasedLongTermMemoryProcessor::ContainsPreferenceSignal(const std::string& text) const
{
    std::string lower = ToLower(text);
    return lower.find("i like") != std::string::npos ||
           lower.find("i prefer") != std::string::npos ||
           lower.find("i want") != std::string::npos ||
           lower.find("please") != std::string::npos ||
           lower.find("always") != std::string::npos ||
           lower.find("never") != std::string::npos;
}

std::string RuleBasedLongTermMemoryProcessor::DetectTopic(const std::string& text) const
{
    std::string lower = ToLower(text);
    std::vector<std::pair<std::string, std::string>> patterns = {
        {"project", "project"},
        {"repository", "project"},
        {"code", "coding"},
        {"api", "api"},
        {"test", "testing"},
        {"deploy", "deployment"},
        {"database", "database"},
        {"config", "configuration"},
        {"security", "security"},
        {"ui", "ui"},
        {"frontend", "ui"},
        {"backend", "backend"},
        {"memory", "memory_system"},
    };
    for (const auto& pattern : patterns) {
        if (lower.find(pattern.first) != std::string::npos) {
            return pattern.second;
        }
    }
    return "";
}

LongTermMemoryUpdate RuleBasedLongTermMemoryProcessor::Process(const LongTermMemoryBatch& batch)
{
    LongTermMemoryUpdate update;
    std::set<std::string> seenTopics;
    bool hasPreference = false;

    std::string agentId;
    for (const auto& event : batch.events) {
        if (agentId.empty() && !event.agentId.empty()) {
            agentId = event.agentId;
        }
    }

    for (const auto& event : batch.events) {
        if (event.type != MemoryEventType::MESSAGE_APPENDED) {
            continue;
        }

        std::string topic = DetectTopic(event.content);
        if (!topic.empty() && seenTopics.find(topic) == seenTopics.end()) {
            seenTopics.insert(topic);
            std::string topicSummary = "Discussed topic: " + topic;
            update.topicSummaries.push_back(topicSummary);

            if (topic != "memory_system") {
                MemoryEntity entity;
                entity.id = "entity:topic." + topic;
                entity.agentId = event.agentId;
                entity.entityType = "topic";
                entity.name = topic + " topic";
                entity.summary = topicSummary;
                entity.confidence = 0.5F;
                entity.sourceRefs = batch.sourceRefs;
                update.entities.push_back(std::move(entity));
            }
        }

        if (!hasPreference && event.role == "user" && ContainsPreferenceSignal(event.content)) {
            hasPreference = true;
            MemoryEntity entity;
            entity.id = "entity:preference.user";
            entity.agentId = event.agentId;
            entity.entityType = "preference";
            entity.name = "User preference";
            entity.summary = "User expressed preferences in session";
            entity.confidence = 0.6F;
            entity.sourceRefs = batch.sourceRefs;
            update.entities.push_back(std::move(entity));

            MemoryRelation relation;
            relation.agentId = event.agentId;
            relation.fromEntityId = "entity:user";
            relation.relationType = "has_preference";
            relation.toEntityId = "entity:preference.user";
            relation.confidence = 0.5F;
            relation.sourceRefs = batch.sourceRefs;
            update.relations.push_back(std::move(relation));
        }
    }

    return update;
}

} // namespace agent_memory
