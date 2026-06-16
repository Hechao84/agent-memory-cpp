#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include "agent_memory/config.h"
#include "agent_memory/payload.h"

namespace agent_memory {

class MemoryPayloadStore;

class PayloadService
{
public:
    PayloadService(const MemoryConfig& config, std::string dataPath, MemoryPayloadStore* store);

    MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request);
    std::string ReadPayload(const std::string& ref) const;
    std::string LastError() const;

private:
    std::string BuildPayloadRef(const MemoryPayloadWriteRequest& request) const;
    std::string BuildPayloadSummary(const MemoryPayloadWriteRequest& request) const;
    bool WritePayloadFileAtomically(const std::filesystem::path& payloadPath, const std::string& content, MemoryPayloadWriteResult& result) const;
    void ScheduleTempFileCleanup() const;
    std::string PayloadDirectory() const;
    void SetLastError(const std::string& error) const;
    void ClearLastError() const;

    const MemoryConfig& config_;
    std::string dataPath_;
    std::string canonicalPayloadDirectory_;
    MemoryPayloadStore* store_;
    mutable std::recursive_mutex mutex_;
    mutable std::string lastError_;
};

} // namespace agent_memory
