#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

/** Single long-term memory search hit. */
struct AGENT_MEMORY_API MemorySearchHit
{
    /** Result id. Summary and relation ids may include type prefixes. */
    std::string id;
    /** Result type such as summary, entity, or relation. */
    std::string type;
    /** Rendered result content. */
    std::string content;
    /** Relevance score; larger means more relevant. Exact scoring is implementation-defined. */
    float score{0.0F};
    /** Source event or session references. */
    std::vector<std::string> sourceRefs;
    /** Implementation metadata, such as score source. */
    nlohmann::json metadata = nlohmann::json::object();
};

/** Request to search long-term memory. */
struct AGENT_MEMORY_API MemorySearchRequest
{
    /** Agent namespace to search. */
    std::string agentId;
    /** Optional session namespace. */
    std::string sessionId;
    /** Search query. Empty query succeeds with no results. */
    std::string query;
    /** Maximum result count. Values <= 0 use the Store default. */
    int limit{10};
    /** Reserved section filter. */
    std::vector<std::string> includeSections;
    /** Caller-defined metadata reserved for future controls. */
    nlohmann::json metadata = nlohmann::json::object();
};

/** Result of SearchMemory. */
struct AGENT_MEMORY_API MemorySearchResult
{
    /** True when search completed successfully. */
    bool succeeded{false};
    /** Search hits. */
    std::vector<MemorySearchHit> hits;
    /** Error information when succeeded is false. */
    MemoryError error;

    /** Returns true when search succeeded. */
    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
