#pragma once

#include <mutex>
#include <string>

#include "agent_memory/config.h"
#include "agent_memory/payload.h"

namespace agent_memory {

class MemoryStore;

class PayloadService
{
public:
    PayloadService(const MemoryConfig& config, std::string dataPath, MemoryStore* store);

    MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request);
    std::string ReadPayload(const std::string& ref) const;
    std::string LastError() const;

private:
    std::string BuildPayloadRef(const MemoryPayloadWriteRequest& request) const;
    std::string BuildPayloadSummary(const MemoryPayloadWriteRequest& request) const;
    std::string PayloadDirectory() const;
    void SetLastError(const std::string& error) const;
    void ClearLastError() const;

    const MemoryConfig& config_;
    std::string dataPath_;
    std::string canonicalPayloadDirectory_;
    MemoryStore* store_;
    mutable std::recursive_mutex mutex_;
    mutable std::string lastError_;
};

} // namespace agent_memory
