#include "payload_query.h"

#include <sstream>

#include "string_util.h"

namespace agent_memory {

namespace {

bool ContainsTerm(const MemoryPayloadRef& payload, const std::string& term)
{
    std::string haystack = ToLower(payload.uri + "\n" + payload.toolName + "\n" + payload.summary + "\n" + payload.contentType);
    return haystack.find(term) != std::string::npos;
}

} // namespace

PayloadQuery ParsePayloadQuery(std::string_view query)
{
    PayloadQuery parsed;
    std::stringstream stream{std::string(query)};
    std::string term;
    while (stream >> term) {
        parsed.terms.push_back(ToLower(term));
    }
    return parsed;
}

bool MatchesPayloadQuery(const MemoryPayloadRef& payload, const PayloadQuery& query)
{
    if (query.empty()) {
        return true;
    }
    for (const auto& term : query.terms) {
        if (!ContainsTerm(payload, term)) {
            return false;
        }
    }
    return true;
}

} // namespace agent_memory
