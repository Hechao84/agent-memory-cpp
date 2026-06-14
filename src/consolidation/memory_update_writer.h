#pragma once

#include <functional>
#include <string>
#include <vector>

#include "long_term_memory_processor.h"

namespace agent_memory {

class MemoryStore;

struct MemoryUpdateWriteResult
{
    bool succeeded{true};
    int savedSummaries{0};
    int savedEntities{0};
    int savedRelations{0};
};

class MemoryUpdateWriter
{
public:
    explicit MemoryUpdateWriter(MemoryStore& store);

    bool RunInTransaction(const std::function<bool()>& work);
    MemoryUpdateWriteResult SaveSessionSummary(const std::string& agentId, const std::string& sessionId,
                                               const std::string& summary,
                                               const std::vector<std::string>& sourceRefs);
    MemoryUpdateWriteResult SaveUpdate(const std::string& agentId, const std::string& sessionId,
                                       const LongTermMemoryUpdate& update,
                                       const std::vector<std::string>& sourceRefs);

private:
    MemoryStore& store_;
};

} // namespace agent_memory
