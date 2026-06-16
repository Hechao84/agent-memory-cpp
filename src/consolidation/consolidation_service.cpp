#include "consolidation_service.h"

#include "consolidation_batch_builder.h"
#include "llm_processor.h"
#include "long_term_memory_processor.h"
#include "memory_update_writer.h"

namespace agent_memory {

ConsolidationService::ConsolidationService(MemoryUpdateWriter& writer, LongTermMemoryProcessor* fallbackProcessor)
    : writer_(writer), fallbackProcessor_(fallbackProcessor)
{
}

MemoryConsolidationResult ConsolidationService::Consolidate(const MemoryConsolidationRequest& request,
                                                            const std::vector<MemoryEvent>& events,
                                                            ModelClient* model)
{
    MemoryConsolidationResult result;
    ConsolidationBatchBuilder batchBuilder;
    ConsolidationBatchBuildResult buildResult = batchBuilder.Build(request, events);
    result.sessionId = buildResult.sessionId;
    result.nextCursor = buildResult.nextCursor;
    result.processedEvents = static_cast<int>(buildResult.batch.events.size());

    if (buildResult.batch.events.empty()) {
        result.succeeded = true;
        return result;
    }

    LongTermMemoryUpdate update;
    if (model != nullptr) {
        LlmLongTermMemoryProcessor llmProcessor(model);
        update = llmProcessor.Process(buildResult.batch);
    }

    bool hasLlmUpdate = !update.topicSummaries.empty() || !update.profileSummaries.empty() ||
                        !update.entities.empty() || !update.relations.empty();
    if (!hasLlmUpdate && fallbackProcessor_ != nullptr) {
        result.fallbackUsed = true;
        update = fallbackProcessor_->Process(buildResult.batch);
    }

    bool hasUpdate = !update.topicSummaries.empty() || !update.profileSummaries.empty() ||
                     !update.entities.empty() || !update.relations.empty();
    MemoryUpdateWriteResult sessionWrite;
    MemoryUpdateWriteResult updateWrite;
    bool ok = writer_.RunInTransaction([&](MemoryStoreTransaction& transaction) {
        sessionWrite = writer_.SaveSessionSummary(transaction, request.agentId, buildResult.sessionId,
                                                 buildResult.sessionSummary, buildResult.sessionSourceRefs);
        if (hasUpdate) {
            updateWrite = writer_.SaveUpdate(transaction, request.agentId, buildResult.sessionId, update,
                                            buildResult.batch.sourceRefs);
        }
        return sessionWrite.succeeded && updateWrite.succeeded;
    });

    result.succeeded = ok;
    if (ok) {
        result.savedSummaries = sessionWrite.savedSummaries + updateWrite.savedSummaries;
        result.savedEntities = updateWrite.savedEntities;
        result.savedRelations = updateWrite.savedRelations;
    }
    if (!ok) {
        result.error = {"consolidation_failed", "failed to persist consolidation update", "", false};
    }
    return result;
}

} // namespace agent_memory
