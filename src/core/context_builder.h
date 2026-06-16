#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "agent_memory/config.h"
#include "agent_memory/context.h"
#include "store.h"

namespace agent_memory {

class MemoryEventStore;
class MemoryLongTermStore;
class MemoryPayloadStore;
class MemorySearchStore;

class ContextBuilder
{
public:
    ContextBuilder(const MemoryConfig& config, MemoryEventStore* eventStore, MemoryPayloadStore* payloadStore,
                   MemoryLongTermStore* longTermStore, MemorySearchStore* searchStore);

    MemoryContextResult BuildContext(const MemoryContextRequest& request) const;

private:
    MemoryEventsResult LoadMessagesForContext(const MemoryContextRequest& request) const;
    MemoryPayloadRefsResult LoadPayloadsForContext(const MemoryContextRequest& request) const;
    bool ShouldInclude(const MemoryContextRequest& request, std::string_view section) const;
    int LongTermMemoryLimit(const MemoryContextRequest& request) const;

    const MemoryConfig& config_;
    MemoryEventStore* eventStore_;
    MemoryPayloadStore* payloadStore_;
    MemoryLongTermStore* longTermStore_;
    MemorySearchStore* searchStore_;
};

} // namespace agent_memory
