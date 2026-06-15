#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "agent_memory/payload.h"

namespace agent_memory {

struct PayloadQuery
{
    std::vector<std::string> terms;

    bool empty() const { return terms.empty(); }
};

PayloadQuery ParsePayloadQuery(std::string_view query);
bool MatchesPayloadQuery(const MemoryPayloadRef& payload, const PayloadQuery& query);

} // namespace agent_memory
