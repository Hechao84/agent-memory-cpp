#include "consolidation_service.h"

#include "consolidation_batch_builder.h"
#include "llm_processor.h"
#include "long_term_memory_processor.h"
#include "memory_update_writer.h"
#include "store.h"

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
        if (buildResult.nextCursor.empty()) {
            result.succeeded = true;
            return result;
        }
        MemoryOperationResult operation = writer_.RunInTransaction([&](MemoryStoreTransaction& transaction) {
            auto cursorWrite = transaction.SaveConsolidationCursor(request.agentId, request.sessionId, buildResult.nextCursor);
            if (!cursorWrite) {
                return cursorWrite.error.HasError()
                           ? cursorWrite
                           : MemoryFailure("cursor_save_failed", "failed to persist consolidation cursor");
            }
            return MemorySuccess();
        });
        result.succeeded = operation.succeeded;
        if (!operation) {
            result.error = operation.error.HasError() ? operation.error
                                                      : MemoryError{"cursor_save_failed", "failed to persist consolidation cursor", "", false};
        }
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
    MemoryOperationResult operation = writer_.RunInTransaction([&](MemoryStoreTransaction& transaction) {
        sessionWrite = writer_.SaveSessionSummary(transaction, request.agentId, buildResult.sessionId,
                                                 buildResult.sessionSummary, buildResult.sessionSourceRefs);
        if (!sessionWrite.succeeded) {
            return MemoryFailure("consolidation_failed", "failed to persist session summary", sessionWrite.error.details,
                                 sessionWrite.error.retryable);
        }
        if (hasUpdate) {
            updateWrite = writer_.SaveUpdate(transaction, request.agentId, buildResult.sessionId, update,
                                            buildResult.batch.sourceRefs);
            if (!updateWrite.succeeded) {
                return MemoryFailure("consolidation_failed", "failed to persist memory update", updateWrite.error.details,
                                     updateWrite.error.retryable);
            }
        }
        if (!buildResult.nextCursor.empty()) {
            auto cursorWrite = transaction.SaveConsolidationCursor(request.agentId, request.sessionId, buildResult.nextCursor);
            if (!cursorWrite) {
                return cursorWrite.error.HasError()
                           ? cursorWrite
                           : MemoryFailure("cursor_save_failed", "failed to persist consolidation cursor");
            }
        }
        return MemorySuccess();
    });

    result.succeeded = operation.succeeded;
    if (operation) {
        result.savedSummaries = sessionWrite.savedSummaries + updateWrite.savedSummaries;
        result.savedEntities = updateWrite.savedEntities;
        result.savedRelations = updateWrite.savedRelations;
    }
    if (!operation) {
        result.error = operation.error.HasError() ? operation.error
                                                  : MemoryError{"consolidation_failed", "failed to persist consolidation update", "", false};
    }
    return result;
}

} // namespace agent_memory
