#include "memory_update_writer.h"

#include "store.h"

namespace agent_memory {

namespace {

MemoryUpdateWriteResult FailedWrite(const MemoryOperationResult& operation)
{
    MemoryUpdateWriteResult result;
    result.succeeded = false;
    result.error = operation.error;
    return result;
}

} // namespace

MemoryUpdateWriter::MemoryUpdateWriter(MemoryLongTermStore& store)
    : store_(store)
{
}

MemoryOperationResult MemoryUpdateWriter::RunInTransaction(const std::function<MemoryOperationResult(MemoryStoreTransaction& transaction)>& work)
{
    return store_.RunInTransaction(work);
}

MemoryUpdateWriteResult MemoryUpdateWriter::SaveSessionSummary(MemoryStoreTransaction& transaction, const std::string& agentId,
                                                               const std::string& sessionId, const std::string& summary,
                                                               const std::vector<std::string>& sourceRefs)
{
    MemoryUpdateWriteResult result;
    auto operation = transaction.SaveSummary(agentId, sessionId, "session", "conversation", summary, 0.5F, sourceRefs);
    if (!operation) {
        return FailedWrite(operation);
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
        auto operation = transaction.SaveSummary(agentId, sessionId, "topic", "auto", topicSummary, 0.5F, sourceRefs);
        if (!operation) {
            return FailedWrite(operation);
        }
        ++result.savedSummaries;
    }
    for (const auto& profileSummary : update.profileSummaries) {
        auto operation = transaction.SaveSummary(agentId, sessionId, "profile", "user", profileSummary, 0.6F, sourceRefs);
        if (!operation) {
            return FailedWrite(operation);
        }
        ++result.savedSummaries;
    }
    for (const auto& entity : update.entities) {
        MemoryEntity e = entity;
        if (e.agentId.empty()) {
            e.agentId = agentId;
        }
        auto operation = transaction.SaveEntity(e);
        if (!operation) {
            return FailedWrite(operation);
        }
        ++result.savedEntities;
        if (!e.supersededEntityId.empty()) {
            auto operation = transaction.MarkEntityObsolete(e.supersededEntityId, e.id);
            if (!operation) {
                return FailedWrite(operation);
            }
        }
    }
    for (const auto& relation : update.relations) {
        MemoryRelation r = relation;
        if (r.agentId.empty()) {
            r.agentId = agentId;
        }
        auto operation = transaction.SaveRelation(r);
        if (!operation) {
            return FailedWrite(operation);
        }
        ++result.savedRelations;
    }
    return result;
}

} // namespace agent_memory
