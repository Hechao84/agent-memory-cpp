#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "agent_memory/config.h"
#include "agent_memory/context.h"

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

    MemoryContextPackage BuildContext(const MemoryContextRequest& request) const;

private:
    std::vector<MemoryMessage> LoadMessagesForContext(const MemoryContextRequest& request) const;
    std::vector<MemoryPayloadRef> LoadPayloadsForContext(const MemoryContextRequest& request) const;
    bool ShouldInclude(const MemoryContextRequest& request, std::string_view section) const;
    int LongTermMemoryLimit(const MemoryContextRequest& request) const;

    const MemoryConfig& config_;
    MemoryEventStore* eventStore_;
    MemoryPayloadStore* payloadStore_;
    MemoryLongTermStore* longTermStore_;
    MemorySearchStore* searchStore_;
};

} // namespace agent_memory
