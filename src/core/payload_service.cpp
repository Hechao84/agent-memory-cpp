#include "payload_service.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "file_util.h"
#include "path_util.h"
#include "store.h"

namespace fs = std::filesystem;

namespace agent_memory {

PayloadService::PayloadService(const MemoryConfig& config, std::string dataPath, MemoryStore* store)
    : config_(config), dataPath_(std::move(dataPath)), store_(store)
{
    canonicalPayloadDirectory_ = CanonicalPath(PayloadDirectory());
    if (canonicalPayloadDirectory_.empty()) {
        SetLastError("failed to canonicalize payload directory path");
    }
}

MemoryPayloadWriteResult PayloadService::WritePayload(const MemoryPayloadWriteRequest& request, int eventCount)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ClearLastError();
    MemoryPayloadWriteResult result;
    result.succeeded = true;
    if (!config_.enablePayloadOffload || request.content.empty()) {
        result.replacementContent = request.content;
        return result;
    }
    if (static_cast<int>(request.content.size()) < config_.offloadThresholdChars) {
        result.replacementContent = request.content;
        return result;
    }

    std::string ref = BuildPayloadRef(request, eventCount);
    fs::path payloadPath = fs::path(PayloadDirectory()) / (ref + ".txt");
    std::error_code error;
    fs::create_directories(payloadPath.parent_path(), error);
    if (error) {
        SetLastError("failed to create payload directory: " + error.message());
        result.succeeded = false;
        result.error = {"payload_write_failed", "failed to create payload directory", error.message(), false};
        result.replacementContent = request.content;
        return result;
    }

    std::ofstream file(payloadPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        SetLastError("failed to open payload file for write");
        result.succeeded = false;
        result.error = {"payload_write_failed", "failed to open payload file for write", "", false};
        result.replacementContent = request.content;
        return result;
    }
    file << request.content;
    file.close();
    if (!file) {
        SetLastError("failed to write payload file");
        result.succeeded = false;
        result.error = {"payload_write_failed", "failed to write payload file", "", false};
        result.replacementContent = request.content;
        return result;
    }

    result.offloaded = true;
    result.payload.agentId = request.agentId;
    result.payload.sessionId = request.sessionId;
    result.payload.uri = "file://" + payloadPath.string();
    result.payload.contentType = request.contentType;
    result.payload.summary = BuildPayloadSummary(request);
    result.payload.toolName = request.toolName;
    result.payload.originalChars = static_cast<int>(request.content.size());
    result.replacementContent = "[memory-ref: " + result.payload.uri + "]\n" + result.payload.summary;
    if (store_ != nullptr) {
        store_->SavePayload(result.payload);
    }
    return result;
}

std::string PayloadService::ReadPayload(const std::string& ref) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ClearLastError();
    if (ref.rfind("file://", 0) != 0) {
        SetLastError("unsupported payload ref scheme");
        return "";
    }

    std::string payloadPath = ref.substr(7);
    if (canonicalPayloadDirectory_.empty()) {
        SetLastError("payload directory path is not resolved");
        return "";
    }
    if (!IsPathInsideDirectory(payloadPath, canonicalPayloadDirectory_)) {
        SetLastError("payload path outside configured payload directory");
        return "";
    }
    std::string content = LoadTextFile(CanonicalPath(payloadPath), true).value_or("");
    if (content.empty()) {
        SetLastError("payload file is empty or unreadable");
    }
    return content;
}

std::string PayloadService::BuildPayloadRef(const MemoryPayloadWriteRequest& request, int eventCount) const
{
    std::string sessionId = request.sessionId.empty() ? "default" : request.sessionId;
    std::string toolCallId = request.toolCallId.empty() ? std::to_string(eventCount + 1) : request.toolCallId;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string ref = sessionId + "_" + toolCallId + "_" + std::to_string(eventCount + 1) + "_" + std::to_string(now);
    for (char& ch : ref) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') {
            ch = '_';
        }
    }
    return ref;
}

std::string PayloadService::BuildPayloadSummary(const MemoryPayloadWriteRequest& request) const
{
    std::stringstream summary;
    summary << "Tool result offloaded";
    if (!request.toolName.empty()) {
        summary << " from " << request.toolName;
    }
    summary << ", original chars: " << request.content.size() << ".";
    return summary.str();
}


std::string PayloadService::PayloadDirectory() const
{
    return (fs::path(dataPath_) / "memory_runtime" / "payloads").string();
}

std::string PayloadService::LastError() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return lastError_;
}

void PayloadService::SetLastError(const std::string& error) const
{
    lastError_ = error;
}

void PayloadService::ClearLastError() const
{
    lastError_.clear();
}

} // namespace agent_memory
