#pragma once

#include <functional>
#include <string>
#include <vector>

#include "agent_memory/error.h"
#include "agent_memory/event.h"
#include "agent_memory/long_term_memory.h"
#include "agent_memory/payload.h"
#include "agent_memory/search.h"
#include "agent_memory/stats.h"


namespace agent_memory {

struct LongTermSummaryRecord
{
    std::string level;
    std::string topic;
    std::string summary;
    float confidence{0.0F};
    std::vector<std::string> sourceRefs;
    std::string createdAt;
    std::string updatedAt;
};

struct LongTermMemorySnapshot
{
    std::vector<LongTermSummaryRecord> summaries;
    std::vector<MemoryEntity> entities;
    std::vector<MemoryRelation> relations;
};

struct MemoryEventsResult
{
    bool succeeded{false};
    std::vector<MemoryEvent> events;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
    size_t size() const { return events.size(); }
    const MemoryEvent& operator[](size_t index) const { return events[index]; }
    std::vector<MemoryEvent>::const_iterator begin() const { return events.begin(); }
    std::vector<MemoryEvent>::const_iterator end() const { return events.end(); }
};

struct MemoryPayloadRefsResult
{
    bool succeeded{false};
    std::vector<MemoryPayloadRef> payloads;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
    size_t size() const { return payloads.size(); }
    const MemoryPayloadRef& operator[](size_t index) const { return payloads[index]; }
    std::vector<MemoryPayloadRef>::const_iterator begin() const { return payloads.begin(); }
    std::vector<MemoryPayloadRef>::const_iterator end() const { return payloads.end(); }
};

struct LongTermMemorySnapshotResult
{
    bool succeeded{false};
    LongTermMemorySnapshot snapshot;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

struct MemorySearchStoreResult
{
    bool succeeded{false};
    std::vector<MemorySearchHit> results;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
    size_t size() const { return results.size(); }
    const MemorySearchHit& operator[](size_t index) const { return results[index]; }
    std::vector<MemorySearchHit>::const_iterator begin() const { return results.begin(); }
    std::vector<MemorySearchHit>::const_iterator end() const { return results.end(); }
};

struct ConsolidationCursorResult
{
    bool succeeded{false};
    std::string cursor;
    MemoryError error;

    explicit operator bool() const { return succeeded; }
};

class MemoryStoreTransaction

{
public:
    virtual ~MemoryStoreTransaction() = default;

    virtual MemoryOperationResult SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                                              const std::string& topic, const std::string& summary, float confidence,
                                              const std::vector<std::string>& sourceRefs = {}) = 0;
    virtual MemoryOperationResult SaveEntity(const MemoryEntity& entity) = 0;
    virtual MemoryOperationResult SaveRelation(const MemoryRelation& relation) = 0;
    virtual MemoryOperationResult MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy) = 0;
    virtual MemoryOperationResult SaveConsolidationCursor(const std::string& agentId, const std::string& sessionId,
                                                          const std::string& cursor) = 0;
};

class MemoryEventStore
{
public:
    virtual ~MemoryEventStore() = default;

    virtual MemoryOperationResult SaveEvent(const MemoryEvent& event) = 0;
    virtual MemoryEventsResult LoadEventsAfterCursor(const std::string& agentId, const std::string& sessionId,
                                                     const std::string& cursor,
                                                     const std::vector<std::string>& excludedSessionIds = {}) const = 0;
    virtual MemoryEventsResult LoadRecentEvents(const std::string& agentId, const std::string& sessionId,
                                                int limit) const = 0;
};

class MemoryPayloadStore
{
public:
    virtual ~MemoryPayloadStore() = default;

    virtual MemoryOperationResult SavePayload(const MemoryPayloadRef& payload) = 0;
    virtual MemoryPayloadRefsResult LoadRecentPayloads(const std::string& agentId, const std::string& sessionId,
                                                       int limit) const = 0;
};

class MemoryLongTermStore
{
public:
    virtual ~MemoryLongTermStore() = default;

    virtual MemoryOperationResult SaveSummary(const std::string& agentId, const std::string& sessionId, const std::string& level,
                                              const std::string& topic, const std::string& summary, float confidence,
                                              const std::vector<std::string>& sourceRefs = {}) = 0;
    virtual MemoryOperationResult SaveEntity(const MemoryEntity& entity) = 0;
    virtual MemoryOperationResult SaveRelation(const MemoryRelation& relation) = 0;
    virtual MemoryOperationResult MarkEntityObsolete(const std::string& entityId, const std::string& supersededBy) = 0;
    virtual MemoryOperationResult RunInTransaction(const std::function<MemoryOperationResult(MemoryStoreTransaction& transaction)>& work) = 0;
    virtual ConsolidationCursorResult LoadConsolidationCursor(const std::string& agentId, const std::string& sessionId) const = 0;
    virtual MemoryOperationResult SaveConsolidationCursor(const std::string& agentId, const std::string& sessionId,
                                                          const std::string& cursor) = 0;
    virtual LongTermMemorySnapshotResult LoadLongTermMemory(const std::string& agentId, int limit,
                                                            const std::string& sessionId = {}) const = 0;
};

class MemorySearchStore
{
public:
    virtual ~MemorySearchStore() = default;

    virtual MemorySearchStoreResult SearchLongTermMemory(const MemorySearchRequest& request) const = 0;
};

class MemoryStatsStore
{
public:
    virtual ~MemoryStatsStore() = default;

    virtual MemoryStatsResult GetStoreStats() const = 0;
};

class MemoryStore : public MemoryEventStore,
                    public MemoryPayloadStore,
                    public MemoryLongTermStore,
                    public MemorySearchStore,
                    public MemoryStatsStore
{
public:
    ~MemoryStore() override = default;

    virtual MemoryOperationResult Initialize() = 0;
};

} // namespace agent_memory
