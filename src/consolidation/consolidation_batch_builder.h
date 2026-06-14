#pragma once

#include <string>
#include <vector>

#include "agent_memory/long_term_memory.h"
#include "long_term_memory_processor.h"

namespace agent_memory {

struct ConsolidationBatchBuildResult
{
    LongTermMemoryBatch batch;
    std::string sessionId;
    std::string sessionSummary;
    std::vector<std::string> sessionSourceRefs;
    std::string nextCursor;
};

class ConsolidationBatchBuilder
{
public:
    ConsolidationBatchBuildResult Build(const MemoryConsolidationRequest& request,
                                        const std::vector<MemoryEvent>& events) const;
};

} // namespace agent_memory
