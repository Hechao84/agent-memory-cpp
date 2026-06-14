#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "agent_memory/config.h"
#include "agent_memory/context.h"

namespace agent_memory {

class MemoryStore;

class ContextBuilder
{
public:
    ContextBuilder(const MemoryConfig& config, MemoryStore* store);

    MemoryContextPackage BuildContext(const MemoryContextRequest& request,
                                      const std::vector<MemoryPayloadRef>& payloads) const;

private:
    std::vector<MemoryMessage> LoadMessagesForContext(const MemoryContextRequest& request) const;
    std::vector<MemoryPayloadRef> LoadPayloadsForContext(const std::vector<MemoryPayloadRef>& payloads,
                                                          const MemoryContextRequest& request) const;
    bool ShouldInclude(const MemoryContextRequest& request, std::string_view section) const;
    bool MatchesQuery(const MemoryPayloadRef& payload, const std::string& query) const;
    int LongTermMemoryLimit(const MemoryContextRequest& request) const;

    const MemoryConfig& config_;
    MemoryStore* store_;
};

} // namespace agent_memory
