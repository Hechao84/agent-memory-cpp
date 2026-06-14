#include "consolidation_batch_builder.h"

#include <sstream>

namespace agent_memory {

ConsolidationBatchBuildResult ConsolidationBatchBuilder::Build(const MemoryConsolidationRequest& request,
                                                               const std::vector<MemoryEvent>& events) const
{
    ConsolidationBatchBuildResult result;
    result.sessionId = request.sessionId;

    if (!request.forceReprocess && request.maxEvents <= 0) {
        return result;
    }

    int limit = request.maxEvents;
    if (request.forceReprocess && limit <= 0) {
        limit = static_cast<int>(events.size());
    }

    std::stringstream summary;
    int count = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        std::string eventCursor = !event.storeCursor.empty() ? event.storeCursor : std::to_string(i + 1);
        result.nextCursor = eventCursor;
        if (!request.sessionId.empty() && event.sessionId != request.sessionId) {
            continue;
        }
        if (event.type != MemoryEventType::MESSAGE_APPENDED) {
            continue;
        }

        summary << event.role << ": " << event.content << "\n";
        result.batch.events.push_back(event);
        result.batch.sourceRefs.push_back("session://" + result.sessionId + "#event:" + eventCursor);
        ++count;
        if (limit > 0 && count >= limit) {
            break;
        }
    }

    result.sessionSummary = summary.str();
    result.sessionSourceRefs.push_back("session://" + result.sessionId);
    return result;
}

} // namespace agent_memory
