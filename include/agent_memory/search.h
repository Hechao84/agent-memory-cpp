#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

struct AGENT_MEMORY_API MemorySearchResult
{
    std::string id;
    std::string type;
    std::string content;
    float score{0.0F};
    std::vector<std::string> sourceRefs;
    nlohmann::json metadata = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemorySearchRequest
{
    std::string agentId;
    std::string sessionId;
    std::string query;
    int limit{10};
    std::vector<std::string> includeSections;
    nlohmann::json metadata = nlohmann::json::object();
};

struct AGENT_MEMORY_API MemorySearchResponse
{
    bool succeeded{false};
    std::vector<MemorySearchResult> results;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
