#include "memory_update_writer.h"

#include "store.h"

namespace agent_memory {

namespace {

MemoryUpdateWriteResult FailedWrite()
{
    MemoryUpdateWriteResult result;
    result.succeeded = false;
    return result;
}

} // namespace

MemoryUpdateWriter::MemoryUpdateWriter(MemoryStore& store)
    : store_(store)
{
}

bool MemoryUpdateWriter::RunInTransaction(const std::function<bool(MemoryStoreTransaction& transaction)>& work)
{
    return store_.RunInTransaction(work);
}

MemoryUpdateWriteResult MemoryUpdateWriter::SaveSessionSummary(MemoryStoreTransaction& transaction, const std::string& agentId,
                                                               const std::string& sessionId, const std::string& summary,
                                                               const std::vector<std::string>& sourceRefs)
{
    MemoryUpdateWriteResult result;
    if (!transaction.SaveSummary(agentId, sessionId, "session", "conversation", summary, 0.5F, sourceRefs)) {
        return FailedWrite();
    }
    result.savedSummaries = 1;
    return result;
}

MemoryUpdateWriteResult MemoryUpdateWriter::SaveUpdate(MemoryStoreTransaction& transaction, const std::string& agentId,
                                                       const std::string& sessionId, const LongTermMemoryUpdate& update,
                                                       const std::vector<std::string>& sourceRefs)
{
    MemoryUpdateWriteResult result;
    for (const auto& topicSummary : update.topicSummaries) {
        if (!transaction.SaveSummary(agentId, sessionId, "topic", "auto", topicSummary, 0.5F, sourceRefs)) {
            return FailedWrite();
        }
        ++result.savedSummaries;
    }
    for (const auto& profileSummary : update.profileSummaries) {
        if (!transaction.SaveSummary(agentId, sessionId, "profile", "user", profileSummary, 0.6F, sourceRefs)) {
            return FailedWrite();
        }
        ++result.savedSummaries;
    }
    for (const auto& entity : update.entities) {
        MemoryEntity e = entity;
        if (e.agentId.empty()) {
            e.agentId = agentId;
        }
        if (!transaction.SaveEntity(e)) {
            return FailedWrite();
        }
        ++result.savedEntities;
        if (!e.supersededEntityId.empty() && !transaction.MarkEntityObsolete(e.supersededEntityId, e.id)) {
            return FailedWrite();
        }
    }
    for (const auto& relation : update.relations) {
        MemoryRelation r = relation;
        if (r.agentId.empty()) {
            r.agentId = agentId;
        }
        if (!transaction.SaveRelation(r)) {
            return FailedWrite();
        }
        ++result.savedRelations;
    }
    return result;
}

} // namespace agent_memory
