#pragma once

#include <vector>

#include "agent_memory/event.h"
#include "agent_memory/long_term_memory.h"
#include "agent_memory/model_client.h"

namespace agent_memory {

class LongTermMemoryProcessor;
class MemoryUpdateWriter;

class ConsolidationService
{
public:
    ConsolidationService(MemoryUpdateWriter& writer, LongTermMemoryProcessor* fallbackProcessor);

    MemoryConsolidationResult Consolidate(const MemoryConsolidationRequest& request, const std::vector<MemoryEvent>& events,
                                          ModelClient* model);

private:
    MemoryUpdateWriter& writer_;
    LongTermMemoryProcessor* fallbackProcessor_;
};

} // namespace agent_memory