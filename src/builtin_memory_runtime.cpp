#include "agent_memory/builtin_memory_runtime.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "long_term_memory_processor.h"
#include "agent_memory/sqlite_store.h"
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace agent_memory {

BuiltinMemoryRuntime::BuiltinMemoryRuntime(MemoryConfig config)
    : MemoryRuntime(std::move(config))
{
    fs::path dbPath = fs::path(ResolveDataPath()) / "memory_runtime" / "memory.db";
    fs::create_directories(dbPath.parent_path());
    sqliteStore_ = std::make_unique<MemorySqliteStore>(dbPath.string());
    sqliteStore_->Initialize();
    longTermProcessor_ = std::make_unique<RuleBasedLongTermMemoryProcessor>();
}

BuiltinMemoryRuntime::~BuiltinMemoryRuntime() = default;

bool BuiltinMemoryRuntime::AppendEvent(const MemoryEvent& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
    if (sqliteStore_) {
        sqliteStore_->SaveEvent(event);
    }
    if (event.type == MemoryEventType::MESSAGE_APPENDED) {
        AppendLegacyHistory(event);
    }
    return true;
}

MemoryContextPackage BuiltinMemoryRuntime::BuildContext(const MemoryContextRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    MemoryContextPackage result;
    result.memoryText = LoadLegacyMemoryText();
    if (sqliteStore_) {
        std::string longTermText = sqliteStore_->LoadLongTermMemoryText(20);
        if (!longTermText.empty()) {
            if (!result.memoryText.empty()) {
                result.memoryText += "\n\n";
            }
            result.memoryText += longTermText;
        }
    }
    result.metadata["provider"] = config_.provider;
    result.metadata["agentId"] = request.agentId;
    result.metadata["sessionId"] = request.sessionId;

    if (!payloads_.empty()) {
        std::stringstream payloadOverview;
        payloadOverview << "\n## Offloaded Payloads\n\n";
        for (const auto& p : payloads_) {
            payloadOverview << "- ref: " << p.ref
                            << ", tool: " << p.toolName
                            << ", chars: " << p.originalChars
                            << ", summary: " << p.summary << "\n";
        }
        result.metadata["payload_count"] = std::to_string(payloads_.size());
        result.memoryText += payloadOverview.str();
        result.payloadRefs = payloads_;
    }
    return result;
}

MemoryPayloadWriteResult BuiltinMemoryRuntime::WritePayload(const MemoryPayloadWriteRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    MemoryPayloadWriteResult result;
    if (!config_.enablePayloadOffload || request.content.empty()) {
        result.replacementContent = request.content;
        return result;
    }
    if (static_cast<int>(request.content.size()) < config_.offloadToolResultChars) {
        result.replacementContent = request.content;
        return result;
    }

    std::string ref = BuildPayloadRef(request);
    fs::path payloadPath = fs::path(ResolveDataPath()) / "memory_runtime" / "payloads" / (ref + ".txt");
    fs::create_directories(payloadPath.parent_path());

    std::ofstream file(payloadPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        result.replacementContent = request.content;
        return result;
    }
    file << request.content;

    result.offloaded = true;
    result.payload.ref = "file://" + payloadPath.string();
    result.payload.contentType = request.contentType;
    result.payload.summary = BuildPayloadSummary(request);
    result.payload.toolName = request.toolName;
    result.payload.originalChars = static_cast<int>(request.content.size());
    result.replacementContent = "[memory-ref: " + result.payload.ref + "]\n" + result.payload.summary;
    payloads_.push_back(result.payload);
    if (sqliteStore_) {
        sqliteStore_->SavePayload(result.payload);
    }
    return result;
}

std::string BuiltinMemoryRuntime::ReadPayload(const std::string& ref)
{
    if (ref.rfind("file://", 0) != 0) {
        return "";
    }
    return LoadFile(ref.substr(7));
}

bool BuiltinMemoryRuntime::Consolidate(const MemoryConsolidationRequest& request)
{
    return Consolidate(request, nullptr);
}

bool BuiltinMemoryRuntime::Consolidate(const MemoryConsolidationRequest& request, MemoryModelClient* model)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sqliteStore_ || events_.empty()) {
        return false;
    }

    std::stringstream summary;
    std::vector<MemoryEvent> batchEvents;
    int count = 0;
    for (const auto& event : events_) {
        if (!request.sessionId.empty() && event.sessionId != request.sessionId) {
            continue;
        }
        if (event.type != MemoryEventType::MESSAGE_APPENDED) {
            continue;
        }
        summary << event.role << ": " << event.content << "\n";
        batchEvents.push_back(event);
        ++count;
        if (count >= request.maxEvents) {
            break;
        }
    }

    if (count == 0) {
        return false;
    }

    std::string sessionId = request.sessionId.empty() ? "default" : request.sessionId;
    std::vector<std::string> sourceRefs;
    sourceRefs.push_back("session://" + sessionId);
    bool ok = sqliteStore_->SaveSummary(request.agentId, sessionId, "session", "conversation",
                                        summary.str(), 0.5F, sourceRefs);

    LongTermMemoryBatch batch;
    batch.events = batchEvents;
    LongTermMemoryUpdate update;
    if (model) {
        LLMLongTermMemoryProcessor llmProcessor(model);
        update = llmProcessor.Process(batch);
    }

    bool hasLlmUpdate = !update.topicSummaries.empty() || !update.profileSummaries.empty() ||
                        !update.entities.empty() || !update.relations.empty();
    if (!hasLlmUpdate && longTermProcessor_) {
        update = longTermProcessor_->Process(batch);
    }

    if (!update.topicSummaries.empty() || !update.profileSummaries.empty() ||
        !update.entities.empty() || !update.relations.empty()) {
        for (const auto& topicSummary : update.topicSummaries) {
            ok = sqliteStore_->SaveSummary(request.agentId, sessionId, "topic", "auto", topicSummary, 0.5F,
                                          sourceRefs) && ok;
        }
        for (const auto& profileSummary : update.profileSummaries) {
            ok = sqliteStore_->SaveSummary(request.agentId, sessionId, "profile", "user", profileSummary, 0.6F,
                                          sourceRefs) && ok;
        }
        for (const auto& entity : update.entities) {
            ok = sqliteStore_->SaveEntity(entity) && ok;
        }
        for (const auto& relation : update.relations) {
            ok = sqliteStore_->SaveRelation(relation) && ok;
        }
    }

    return ok;
}

std::vector<MemorySearchResult> BuiltinMemoryRuntime::SearchMemory(const MemorySearchRequest& request)
{
    std::vector<MemorySearchResult> results;
    std::string memoryText = LoadLegacyMemoryText();
    if (memoryText.empty() || request.query.empty()) {
        return results;
    }
    if (memoryText.find(request.query) == std::string::npos) {
        return results;
    }

    MemorySearchResult result;
    result.id = "legacy.memory";
    result.type = "legacy_text";
    result.content = memoryText;
    result.score = 1.0F;
    results.push_back(std::move(result));
    return results;
}

MemoryStats BuiltinMemoryRuntime::GetStats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    MemoryStats stats;
    stats.events = sqliteStore_ ? sqliteStore_->CountRows("memory_events") : static_cast<int>(events_.size());
    stats.payloads = sqliteStore_ ? sqliteStore_->CountRows("memory_payloads") : static_cast<int>(payloads_.size());
    stats.summaries = sqliteStore_ ? sqliteStore_->CountRows("memory_summaries") : 0;
    stats.entities = sqliteStore_ ? sqliteStore_->CountRows("memory_entities") : 0;
    stats.relations = sqliteStore_ ? sqliteStore_->CountRows("memory_relations") : 0;
    stats.metadata["provider"] = config_.provider;
    return stats;
}

std::string BuiltinMemoryRuntime::ResolveDataPath() const
{
    if (!config_.dataPath.empty()) {
        return config_.dataPath;
    }
    return "./data";
}

std::string BuiltinMemoryRuntime::BuildPayloadRef(const MemoryPayloadWriteRequest& request) const
{
    std::string sessionId = request.sessionId.empty() ? "default" : request.sessionId;
    std::string toolCallId = request.toolCallId.empty() ? std::to_string(events_.size() + 1) : request.toolCallId;
    std::string ref = sessionId + "_" + toolCallId;
    for (char& ch : ref) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') {
            ch = '_';
        }
    }
    return ref;
}

std::string BuiltinMemoryRuntime::BuildPayloadSummary(const MemoryPayloadWriteRequest& request) const
{
    std::stringstream summary;
    summary << "Tool result offloaded";
    if (!request.toolName.empty()) {
        summary << " from " << request.toolName;
    }
    summary << ", original chars: " << request.content.size() << ".";
    return summary.str();
}

int BuiltinMemoryRuntime::ReadLegacyCursor(const std::string& cursorFile) const
{
    std::ifstream file(cursorFile);
    if (!file.is_open()) {
        return 0;
    }

    std::string line;
    if (!std::getline(file, line)) {
        return 0;
    }

    try {
        return std::stoi(line);
    } catch (...) {
        return 0;
    }
}

int BuiltinMemoryRuntime::AppendLegacyHistory(const MemoryEvent& event)
{
    fs::path memoryDir = fs::path(ResolveDataPath()) / "memory";
    fs::create_directories(memoryDir);

    std::string cursorFile = (memoryDir / ".cursor").string();
    int cursor = ReadLegacyCursor(cursorFile) + 1;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::gmtime(&time), "%Y-%m-%d %H:%M");

    nlohmann::json entry;
    entry["cursor"] = cursor;
    entry["timestamp"] = timestamp.str();
    entry["session_id"] = event.sessionId;
    entry["role"] = event.role;
    entry["content"] = event.content;

    if (!event.toolCallId.empty()) {
        entry["tool_call_id"] = event.toolCallId;
    }
    if (!event.toolName.empty()) {
        entry["tool_name"] = event.toolName;
    }
    if (!event.payloadRef.empty()) {
        entry["payload_ref"] = event.payloadRef;
    }

    std::ofstream historyFile(memoryDir / "history.jsonl", std::ios::app);
    if (historyFile.is_open()) {
        historyFile << entry.dump() << "\n";
    }

    std::ofstream cursorOut(cursorFile, std::ios::trunc);
    if (cursorOut.is_open()) {
        cursorOut << cursor;
    }

    return cursor;
}

std::string BuiltinMemoryRuntime::LoadFile(const std::string& path) const
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string BuiltinMemoryRuntime::LoadLegacyMemoryText() const
{
    fs::path basePath = ResolveDataPath();
    std::vector<std::pair<std::string, fs::path>> files = {
        {"MEMORY", basePath / "memory" / "MEMORY.md"},
        {"SOUL", basePath / "SOUL.md"},
        {"USER", basePath / "USER.md"},
    };

    std::stringstream result;
    for (const auto& item : files) {
        std::string content = LoadFile(item.second.string());
        if (content.empty()) {
            continue;
        }
        if (result.tellp() > 0) {
            result << "\n\n";
        }
        result << "## " << item.first << "\n" << content;
    }
    return result.str();
}

} // namespace agent_memory
