#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_memory/error.h"
#include "agent_memory/export.h"

namespace agent_memory {

/** Long-term memory entity extracted from event history. */
struct AGENT_MEMORY_API MemoryEntity
{
    /** Stable entity id within an agent namespace. */
    std::string id;
    /** Agent namespace for the entity. */
    std::string agentId;
    /** Entity category such as preference, topic, project, task, file, user, or style. */
    std::string entityType;
    /** Short display name. */
    std::string name;
    /** Human-readable entity summary. */
    std::string summary;
    /** Confidence score in the range [0, 1]. Stored as float because precision requirements are low. */
    float confidence{0.0F};
    /** Whether the entity is currently active. */
    bool isActive{true};
    /** Replacement entity id when this entity has been superseded. */
    std::string supersededByEntityId;
    /** Previous entity id superseded by this entity. */
    std::string supersededEntityId;
    /** Source event or session references supporting this entity. */
    std::vector<std::string> sourceRefs;
    /** Caller/provider-defined metadata. */
    nlohmann::json metadata = nlohmann::json::object();
    /** Store-assigned creation timestamp. */
    std::string createdAt;
    /** Store-assigned update timestamp. */
    std::string updatedAt;
};

/** Relationship between two long-term memory entities. */
struct AGENT_MEMORY_API MemoryRelation
{
    /** Store-assigned relation id. */
    std::string id;
    /** Agent namespace for the relation. */
    std::string agentId;
    /** Source entity id. */
    std::string fromEntityId;
    /** Relation type such as prefers, works_on, related_to, supersedes, or contradicts. */
    std::string relationType;
    /** Target entity id. */
    std::string toEntityId;
    /** Confidence score in the range [0, 1]. Stored as float because precision requirements are low. */
    float confidence{0.0F};
    /** Source event or session references supporting this relation. */
    std::vector<std::string> sourceRefs;
    /** Caller/provider-defined metadata. */
    nlohmann::json metadata = nlohmann::json::object();
    /** Store-assigned creation timestamp. */
    std::string createdAt;
    /** Store-assigned update timestamp. */
    std::string updatedAt;
};

/** Request to consolidate short-term events into long-term memory. */
struct AGENT_MEMORY_API MemoryConsolidationRequest
{
    /** Agent namespace to process. */
    std::string agentId;
    /** Optional session namespace. Empty processes the agent-level scope used by the Store. */
    std::string sessionId;
    /** Maximum events to process. Values <= 0 mean no batch truncation. */
    int maxEvents{100};
    /** When true, ignores the saved cursor and reprocesses from the beginning. */
    bool forceReprocess{false};
    /** Session ids whose events must be skipped by the batch builder. Events
     *  belonging to these sessions are still persisted by the Store (audit
     *  trail) and still advance the cursor, but they never enter the
     *  consolidation batch and are not seen by the LLM/rule-based
     *  processors. Empty by default to preserve the legacy behavior of
     *  processing every session. */
    std::vector<std::string> excludedSessionIds;
    /** Caller-defined metadata reserved for future controls. */
    nlohmann::json metadata = nlohmann::json::object();
};

/** Result of a consolidation run. */
struct AGENT_MEMORY_API MemoryConsolidationResult
{
    /** True when consolidation completed successfully, including no-op runs with no events. */
    bool succeeded{false};
    /** True when rule-based extraction was used after model unavailability or empty model output. */
    bool fallbackUsed{false};
    /** Number of events processed. */
    int processedEvents{0};
    /** Number of summaries saved. */
    int savedSummaries{0};
    /** Number of entities saved. */
    int savedEntities{0};
    /** Number of relations saved. */
    int savedRelations{0};
    /** Cursor to persist for the next incremental run. Empty when no new events were processed. */
    std::string nextCursor;
    /** Session id represented by this run. */
    std::string sessionId;
    /** Error information when succeeded is false. */
    MemoryError error;

    /** Returns true when consolidation succeeded. */
    explicit operator bool() const { return succeeded; }
};

} // namespace agent_memory
