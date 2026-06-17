#include "payload_service.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>
#include <utility>

#include "file_util.h"
#include "path_util.h"
#include "store.h"

namespace fs = std::filesystem;

namespace agent_memory {

namespace {

constexpr std::chrono::hours kTempFileCleanupTtl{24};

std::string RandomHexSuffix()
{
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned long long> distribution;
    std::stringstream suffix;
    suffix << std::hex << distribution(generator) << distribution(generator);
    return suffix.str();
}

bool IsExpired(const fs::directory_entry& entry, std::chrono::hours ttl)
{
    std::error_code error;
    auto writeTime = entry.last_write_time(error);
    if (error) {
        return false;
    }
    return fs::file_time_type::clock::now() - writeTime > ttl;
}

} // namespace

PayloadService::PayloadService(const MemoryConfig& config, std::string dataPath, MemoryPayloadStore* store)
    : config_(config), dataPath_(std::move(dataPath)), store_(store)
{
    canonicalPayloadDirectory_ = CanonicalPath(PayloadDirectory());
    if (canonicalPayloadDirectory_.empty()) {
    }
}

MemoryPayloadWriteResult PayloadService::WritePayload(const MemoryPayloadWriteRequest& request)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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

    if (store_ == nullptr) {
        result.succeeded = false;
        result.offloaded = false;
        result.error = MemoryError{"payload_store_unavailable",
                              "payload offload requires a persistent store for metadata", "", false};
        result.replacementContent = request.content;
        return result;
    }
    ScheduleTempFileCleanup();

    std::string ref = BuildPayloadRef(request);
    fs::path payloadPath = fs::path(PayloadDirectory()) / (ref + ".txt");
    if (!WritePayloadFileAtomically(payloadPath, request.content, result)) {
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

    auto saveResult = store_->SavePayload(result.payload);
    if (!saveResult) {
        std::error_code removeError;
        fs::remove(payloadPath, removeError);
        result.succeeded = false;
        result.offloaded = false;
        result.error = saveResult.error.HasError() ? saveResult.error
                                                   : MemoryError{"payload_write_failed", "failed to persist payload metadata", "", false};
        result.replacementContent = request.content;
    }
    return result;
}

MemoryPayloadReadResult PayloadService::ReadPayload(const std::string& ref) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (ref.rfind("file://", 0) != 0) {
        return {false, {}, {"payload_read_failed", "unsupported payload ref scheme", "", false}};
    }

    std::string payloadPath = ref.substr(7);
    if (canonicalPayloadDirectory_.empty()) {
        return {false, {}, {"payload_read_failed", "payload directory path is not resolved", "", false}};
    }
    if (!IsPathInsideDirectory(payloadPath, canonicalPayloadDirectory_)) {
        return {false, {}, {"payload_read_failed", "payload path outside configured payload directory", "", false}};
    }
    std::string content = LoadTextFile(CanonicalPath(payloadPath), true).value_or("");
    if (content.empty()) {
        return {false, {}, {"payload_read_failed", "payload file is empty or unreadable", "", false}};
    }
    return {true, content, {}};
}

std::string PayloadService::BuildPayloadRef(const MemoryPayloadWriteRequest& request) const
{
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned long long> distribution;
    std::stringstream ref;
    std::string sessionId = request.sessionId.empty() ? "default" : request.sessionId;
    std::string toolCallId = request.toolCallId.empty() ? "payload" : request.toolCallId;
    ref << sessionId << "_" << toolCallId << "_" << std::hex << distribution(generator) << distribution(generator);
    std::string value = ref.str();
    for (char& ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') {
            ch = '_';
        }
    }
    return value;
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

bool PayloadService::WritePayloadFileAtomically(const std::filesystem::path& payloadPath, const std::string& content,
                                                MemoryPayloadWriteResult& result) const
{
    std::error_code error;
    fs::create_directories(payloadPath.parent_path(), error);
    if (error) {
        result.succeeded = false;
        result.error = {"payload_write_failed", "failed to create payload directory", error.message(), false};
        return false;
    }

    if (fs::exists(payloadPath, error)) {
        result.succeeded = false;
        result.error = {"payload_write_failed", "payload file already exists", "", false};
        return false;
    }

    fs::path tempPath = payloadPath;
    tempPath += ".tmp." + RandomHexSuffix();
    if (fs::exists(tempPath, error)) {
        result.succeeded = false;
        result.error = {"payload_write_failed", "payload temp file already exists", "", false};
        return false;
    }

    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        result.succeeded = false;
        result.error = {"payload_write_failed", "failed to open payload temp file for write", "", false};
        return false;
    }
    file << content;
    file.close();
    if (!file) {
        std::error_code removeError;
        fs::remove(tempPath, removeError);
        result.succeeded = false;
        result.error = {"payload_write_failed", "failed to write payload temp file", "", false};
        return false;
    }

    fs::rename(tempPath, payloadPath, error);
    if (error) {
        std::error_code removeError;
        fs::remove(tempPath, removeError);
        result.succeeded = false;
        result.error = {"payload_write_failed", "failed to finalize payload file", error.message(), false};
        return false;
    }
    return true;
}

void PayloadService::ScheduleTempFileCleanup() const
{
    fs::path directory = PayloadDirectory();
    std::thread([directory]() {
        std::error_code error;
        if (fs::exists(directory, error) && !error) {
            for (const auto& entry : fs::directory_iterator(directory, error)) {
                if (error) {
                    break;
                }
                std::string name = entry.path().filename().string();
                if (!entry.is_regular_file(error) || error || name.find(".txt.tmp.") == std::string::npos ||
                    !IsExpired(entry, kTempFileCleanupTtl)) {
                    error.clear();
                    continue;
                }
                std::error_code removeError;
                fs::remove(entry.path(), removeError);
            }
        }
    }).detach();
}

std::string PayloadService::PayloadDirectory() const
{
    return (fs::path(dataPath_) / "memory_runtime" / "payloads").string();
}

} // namespace agent_memory
