#pragma once

#include <string>
#include <vector>

#include "agent_memory/event.h"
#include "agent_memory/long_term_memory.h"

namespace agent_memory {

struct LongTermMemoryBatch
{
    std::vector<MemoryEvent> events;
    std::vector<std::string> sourceRefs;
};

struct LongTermMemoryUpdate
{
    std::vector<MemoryEntity> entities;
    std::vector<MemoryRelation> relations;
    std::vector<std::string> profileSummaries;
    std::vector<std::string> topicSummaries;
};

class LongTermMemoryProcessor
{
public:
    virtual ~LongTermMemoryProcessor() = default;
    virtual LongTermMemoryUpdate Process(const LongTermMemoryBatch& batch) = 0;
};

} // namespace agent_memory
