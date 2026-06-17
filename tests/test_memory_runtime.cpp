#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

#include <thread>
#include <vector>

#include "agent_memory/builtin_memory_runtime.h"
#include "context_builder.h"
#include "curl_client.h"
#include "file_util.h"
#include "path_util.h"
#include "payload_query.h"
#include "payload_service.h"
#include "runtime_paths.h"
#include "runtime_store_initializer.h"
#include "sqlite_store.h"
#include "store.h"

namespace fs = std::filesystem;

using namespace agent_memory;

namespace {

class FailingPayloadStore : public MemoryPayloadStore
{
public:
    MemoryOperationResult SavePayload(const MemoryPayloadRef&) override { return MemoryFailure("payload_store_failed", "payload metadata failed", "forced failure"); }
    MemoryPayloadRefsResult LoadRecentPayloads(const std::string&, const std::string&, int) const override { return {true, {}, {}}; }
};

class StaticModelClient : public ModelClient
{
public:
    explicit StaticModelClient(std::string response) : response_(std::move(response)) {}

    ModelInvokeResult GenerateMemoryUpdate(const std::string&) override
    {
        ++calls;
        ModelInvokeResult result;
        result.text = response_;
        return result;
    }

    int calls{0};

private:
    std::string response_;
};

bool TestDirectServiceCoverage()
{
    CurlGlobalScope curlScope;
    if (CurlClient::UrlEscape("a b%") != "a%20b%25") {
        std::cerr << "CurlClient UrlEscape failed\n";
        return false;
    }

    fs::path root = fs::temp_directory_path() / "agent_memory_cpp_direct_service_test";
    fs::remove_all(root);

    MemoryConfig config;
    config.dataPath = root.string();
    config.enablePayloadOffload = true;
    config.offloadThresholdChars = 5;

    fs::create_directories(root / "memory_runtime");
    MemorySqliteStore store((root / "memory_runtime" / "memory.db").string());
    if (!store.Initialize()) {
        std::cerr << "direct sqlite store init failed\n";
        return false;
    }

    PayloadService payloadService(config, config.dataPath, &store);
    MemoryPayloadWriteRequest smallPayload;
    smallPayload.agentId = "agent-direct";
    smallPayload.sessionId = "session-direct";
    smallPayload.content = "tiny";
    smallPayload.contentType = "text/plain";
    auto inlinePayload = payloadService.WritePayload(smallPayload);
    if (!inlinePayload || inlinePayload.offloaded || inlinePayload.replacementContent != "tiny") {
        std::cerr << "direct PayloadService inline write failed\n";
        return false;
    }

    MemoryPayloadWriteRequest largePayload = smallPayload;
    largePayload.content = "large payload content";
    largePayload.toolName = "DirectTool";
    auto offloadedPayload = payloadService.WritePayload(largePayload);
    if (!offloadedPayload || !offloadedPayload.offloaded || offloadedPayload.payload.uri.empty()) {
        std::cerr << "direct PayloadService offload failed\n";
        return false;
    }
    auto readPayload = payloadService.ReadPayload(offloadedPayload.payload.uri);
    if (!readPayload || readPayload.content != largePayload.content) {
        std::cerr << "direct PayloadService read failed\n";
        return false;
    }

    MemoryEvent directEvent;
    directEvent.type = MemoryEventType::MESSAGE_APPENDED;
    directEvent.agentId = "agent-direct";
    directEvent.sessionId = "session-direct";
    directEvent.role = "user";
    directEvent.content = "direct context message";
    if (!store.SaveEvent(directEvent)) {
        std::cerr << "direct context event setup failed\n";
        return false;
    }
    if (!store.SaveSummary("agent-direct", "session-direct", "session", "direct", "Direct long-term summary", 0.7F, {"event:1"})) {
        std::cerr << "direct context summary setup failed\n";
        return false;
    }

    ContextBuilder builder(config, &store, &store, &store, &store);
    MemoryContextRequest request;
    request.agentId = "agent-direct";
    request.sessionId = "session-direct";
    auto context = builder.BuildContext(request);
    if (!context) {
        std::cerr << "direct ContextBuilder should succeed\n";
        return false;
    }
    if (context.context.messages.empty()) {
        std::cerr << "direct ContextBuilder should load messages\n";
        return false;
    }
    if (context.context.payloadRefs.empty()) {
        std::cerr << "direct ContextBuilder should load payload refs\n";
        return false;
    }
    if (context.context.memoryText.find("Direct long-term summary") == std::string::npos) {
        std::cerr << "direct ContextBuilder should load long-term summaries\n";
        return false;
    }

    fs::remove_all(root);
    return true;
}

bool TestUtilityModules()
{
    fs::path root = fs::temp_directory_path() / "agent_memory_cpp_utility_test";
    fs::remove_all(root);
    fs::create_directories(root / "payloads" / "nested");
    std::ofstream(root / "sample.txt") << "hello";

    if (LoadTextFile((root / "sample.txt").string()).value_or("") != "hello") {
        std::cerr << "LoadTextFile should read text files\n";
        return false;
    }
    if (LoadTextFile((root / "missing.txt").string()).has_value()) {
        std::cerr << "LoadTextFile should return nullopt for missing files\n";
        return false;
    }
    if (CanonicalPath((root / "." / "payloads").string()).empty()) {
        std::cerr << "CanonicalPath should resolve existing paths\n";
        return false;
    }
    if (!IsPathInsideDirectory((root / "payloads" / "nested").string(), (root / "payloads").string()) ||
        IsPathInsideDirectory((root / "payloads").string(), (root / "payloads").string()) ||
        IsPathInsideDirectory((root / "sample.txt").string(), (root / "payloads").string()) ||
        IsPathInsideDirectory((root / "payloads" / ".." / "sample.txt").string(), (root / "payloads").string())) {
        std::cerr << "IsPathInsideDirectory should reject equality and traversal\n";
        return false;
    }

    MemoryPayloadRef payload;
    payload.uri = "file://payloads/tool.txt";
    payload.toolName = "SearchTool";
    payload.summary = "Large JSON Result";
    payload.contentType = "application/json";
    if (!MatchesPayloadQuery(payload, ParsePayloadQuery("searchtool json")) ||
        !MatchesPayloadQuery(payload, ParsePayloadQuery("LARGE result")) ||
        MatchesPayloadQuery(payload, ParsePayloadQuery("missing"))) {
        std::cerr << "payload query matching failed\n";
        return false;
    }

    MemoryConfig defaultConfig;
    if (ResolveRuntimeDataPath(defaultConfig) != "./data") {
        std::cerr << "default runtime data path failed\n";
        return false;
    }
    MemoryConfig configured;
    configured.dataPath = root.string();
    if (RuntimeDatabasePath(configured) != root / "memory_runtime" / "memory.db") {
        std::cerr << "runtime database path failed\n";
        return false;
    }
    auto store = CreateRuntimeStore(configured);
    if (!store || !fs::exists(root / "memory_runtime" / "memory.db")) {
        std::cerr << "runtime store initializer should create database\n";
        return false;
    }
    MemoryConfig badConfig;
    badConfig.dataPath = (root / "sample.txt").string();
    auto badStore = CreateRuntimeStore(badConfig);
    if (badStore || badStore.error.find("failed") == std::string::npos) {
        std::cerr << "runtime store initializer should fail for file data path\n";
        return false;
    }

    fs::remove_all(root);
    return true;
}

} // namespace

int main()
{
    if (!TestUtilityModules()) {
        return 1;
    }
    if (!TestDirectServiceCoverage()) {
        return 1;
    }

    fs::path dataPath = fs::temp_directory_path() / "agent_memory_cpp_test";
    fs::remove_all(dataPath);

    MemoryConfig config;
    fs::remove_all(dataPath);

    {
        MemoryConfig badConfig;
        badConfig.dataPath = "/dev/null/";
        BuiltinMemoryRuntime badRuntime(badConfig);
        auto badStats = badRuntime.GetStats();
        if (badStats || badStats.error.code != "stats_unavailable" || badStats.error.message.find("failed to") == std::string::npos) {
            std::cerr << "init failure should expose store error in stats\n";
            return 1;
        }
        MemoryEvent badEvent;
        badEvent.type = MemoryEventType::MESSAGE_APPENDED;
        badEvent.agentId = "agent-1";
        badEvent.sessionId = "session-1";
        badEvent.role = "user";
        badEvent.content = "should fail";
        auto failedAppend = badRuntime.AppendEvent(badEvent);
        if (failedAppend || failedAppend.error.code != "store_unavailable" || failedAppend.error.message.find("failed to") == std::string::npos) {
            std::cerr << "AppendEvent should fail with store init error\n";
            return 1;
        }
    }

    config.dataPath = dataPath.string();
    config.enablePayloadOffload = true;
    config.offloadThresholdChars = 10;

    BuiltinMemoryRuntime runtime(config);
    auto initialModelStatus = runtime.GetModelStatus();
    if (initialModelStatus.configured || initialModelStatus.available || !initialModelStatus.error.empty()) {
        std::cerr << "default model status should be unconfigured\n";
        return 1;
    }

    auto invalidModelPath = fs::temp_directory_path() / "agent_memory_cpp_invalid_model_status_test";
    fs::remove_all(invalidModelPath);
    MemoryConfig invalidModelConfig;
    invalidModelConfig.dataPath = invalidModelPath.string();
    invalidModelConfig.model.enabled = true;
    invalidModelConfig.model.formatType = "openai";
    invalidModelConfig.model.modelName = "missing-base-url";
    BuiltinMemoryRuntime invalidModelRuntime(invalidModelConfig);
    auto invalidModelStatus = invalidModelRuntime.GetModelStatus();
    if (!invalidModelStatus.configured || invalidModelStatus.available || invalidModelStatus.error.empty()) {
        std::cerr << "invalid builtin model status should expose load error\n";
        return 1;
    }
    fs::remove_all(invalidModelPath);

    MemoryEvent event;
    event.type = MemoryEventType::MESSAGE_APPENDED;
    event.agentId = "agent-1";
    event.sessionId = "session-1";
    event.role = "user";
    event.content = "I prefer concise answers about code testing";

    if (!runtime.AppendEvent(event)) {
        std::cerr << "AppendEvent failed\n";
        return 1;
    }

    MemoryPayloadWriteRequest payloadRequest;
    payloadRequest.agentId = "agent-1";
    payloadRequest.sessionId = "session-1";
    payloadRequest.content = "01234567890123456789";
    payloadRequest.contentType = "tool_result";
    payloadRequest.toolCallId = "tool-1";
    payloadRequest.toolName = "test_tool";

    auto payloadResult = runtime.WritePayload(payloadRequest);
    if (!payloadResult.offloaded || runtime.ReadPayload(payloadResult.payload.uri).content != payloadRequest.content ||
        payloadResult.payload.agentId != "agent-1" || payloadResult.payload.sessionId != "session-1") {
        std::cerr << "payload offload failed\n";
        return 1;
    }
    auto duplicatePayloadResult = runtime.WritePayload(payloadRequest);
    if (!duplicatePayloadResult.offloaded || duplicatePayloadResult.payload.uri == payloadResult.payload.uri) {
        std::cerr << "duplicate payload refs should not collide\n";
        return 1;
    }
    fs::path payloadDirectory = dataPath / "memory_runtime" / "payloads";
    for (const auto& entry : fs::directory_iterator(payloadDirectory)) {
        if (entry.path().filename().string().find(".txt.tmp.") != std::string::npos) {
            std::cerr << "payload temp file should not remain after successful write\n";
            return 1;
        }
    }
    auto oldTemp = payloadDirectory / "stale.txt.tmp.test";
    {
        std::ofstream staleFile(oldTemp);
        staleFile << "stale";
    }
    fs::last_write_time(oldTemp, fs::file_time_type::clock::now() - std::chrono::hours(25));
    auto cleanupTrigger = runtime.WritePayload(payloadRequest);
    if (!cleanupTrigger.offloaded) {
        std::cerr << "payload cleanup trigger write failed\n";
        return 1;
    }
    for (int i = 0; i < 50 && fs::exists(oldTemp); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (fs::exists(oldTemp)) {
        std::cerr << "stale payload temp file should be cleaned asynchronously\n";
        return 1;
    }
    auto failDataPath = fs::temp_directory_path() / "agent_memory_cpp_payload_fail_test";
    fs::remove_all(failDataPath);
    MemoryConfig failConfig;
    failConfig.dataPath = failDataPath.string();
    failConfig.enablePayloadOffload = true;
    failConfig.offloadThresholdChars = 1;
    FailingPayloadStore failingStore;
    PayloadService failingPayloadService(failConfig, failConfig.dataPath, &failingStore);
    auto failedPayload = failingPayloadService.WritePayload(payloadRequest);
    if (failedPayload || failedPayload.error.code != "payload_store_failed" || failedPayload.error.details != "forced failure") {
        std::cerr << "payload metadata failure should fail write result\n";
        return 1;
    }
    fs::path failedPayloadDirectory = failDataPath / "memory_runtime" / "payloads";
    if (fs::exists(failedPayloadDirectory) && !fs::is_empty(failedPayloadDirectory)) {
        std::cerr << "payload file should be removed after metadata failure\n";
        return 1;
    }
    fs::remove_all(failDataPath);
    auto otherPayloadRequest = payloadRequest;
    otherPayloadRequest.agentId = "agent-2";
    otherPayloadRequest.sessionId = "session-2";
    otherPayloadRequest.toolCallId = "tool-2";
    otherPayloadRequest.content = "abcdefghijklmnopqrstuv";
    auto otherPayloadResult = runtime.WritePayload(otherPayloadRequest);
    if (!otherPayloadResult.offloaded || otherPayloadResult.payload.agentId != "agent-2" || otherPayloadResult.payload.sessionId != "session-2") {
        std::cerr << "payload ownership fields failed\n";
        return 1;
    }
    auto badPayloadRead = runtime.ReadPayload("file:///etc/passwd");
    if (badPayloadRead || badPayloadRead.error.code != "payload_read_failed") {
        std::cerr << "payload path traversal should be rejected\n";
        return 1;
    }
     auto missingPayloadRead = runtime.ReadPayload("file://" + (dataPath / "memory_runtime" / "payloads" / "missing.txt").string());
     if (missingPayloadRead || missingPayloadRead.error.code != "payload_read_failed") {
         std::cerr << "missing payload file should fail with structured error\n";
         return 1;
     }
     // Test offload with null payload store: should fail immediately without writing file
     fs::path nullStoreDataPath = fs::temp_directory_path() / "agent_memory_cpp_null_store_test";
     fs::remove_all(nullStoreDataPath);
     MemoryConfig nullStoreConfig;
     nullStoreConfig.dataPath = nullStoreDataPath.string();
     nullStoreConfig.enablePayloadOffload = true;
     nullStoreConfig.offloadThresholdChars = 1;
     PayloadService nullStoreService(nullStoreConfig, nullStoreConfig.dataPath, nullptr);
     auto nullStoreResult = nullStoreService.WritePayload(payloadRequest);
     if (nullStoreResult.succeeded || nullStoreResult.offloaded || nullStoreResult.error.code != "payload_store_unavailable") {
         std::cerr << "offload with null store should fail immediately with payload_store_unavailable\n";
         return 1;
     }
     if (nullStoreResult.replacementContent != payloadRequest.content) {
         std::cerr << "null store offload failure should return original content as replacement\n";
         return 1;
     }
     fs::path nullPayloadDirectory = nullStoreDataPath / "memory_runtime" / "payloads";
     bool hasFiles = false;
     if (fs::exists(nullPayloadDirectory)) {
         for (auto const& entry : fs::directory_iterator(nullPayloadDirectory)) {
             (void)entry;
             hasFiles = true;
             break;
         }
     }
     if (hasFiles) {
         std::cerr << "null store offload failure should not create orphan payload files\n";
         return 1;
     }
     fs::remove_all(nullStoreDataPath);

     MemoryConsolidationRequest consolidateRequest;
    consolidateRequest.agentId = "agent-1";
    consolidateRequest.sessionId = "session-1";
    auto consolidationResult = runtime.Consolidate(consolidateRequest);
    if (!consolidationResult || consolidationResult.processedEvents != 1 || consolidationResult.nextCursor.empty()) {
        std::cerr << "Consolidate failed\n";
        return 1;
    }
    MemorySearchRequest searchRequest;
    searchRequest.agentId = "agent-1";
    searchRequest.sessionId = "session-1";
    searchRequest.query = "concise";
    searchRequest.limit = 5;
    if (runtime.SearchMemory(searchRequest).results.empty()) {
        std::cerr << "SQLite memory search failed\n";
        return 1;
    }
    auto emptySearchRequest = searchRequest;
    emptySearchRequest.query.clear();
    auto emptySearch = runtime.SearchMemory(emptySearchRequest);
    if (!emptySearch || !emptySearch.results.empty()) {
        std::cerr << "empty search query should succeed with no results\n";
        return 1;
    }

    auto repeatedConsolidationResult = runtime.Consolidate(consolidateRequest);
    if (!repeatedConsolidationResult.succeeded || repeatedConsolidationResult.processedEvents != 0) {
        std::cerr << "incremental consolidation repeated already processed events\n";
        return 1;
    }
    auto forcedRequest = consolidateRequest;
    forcedRequest.forceReprocess = true;
    auto forcedResult = runtime.Consolidate(forcedRequest);
    if (!forcedResult || forcedResult.processedEvents != 1) {
        std::cerr << "forced consolidation should reprocess events\n";
        return 1;
    }
    auto afterForceResult = runtime.Consolidate(consolidateRequest);
    if (!afterForceResult.succeeded || afterForceResult.processedEvents != 0) {
        std::cerr << "forced consolidation should advance cursor\n";
        return 1;
    }

    BuiltinMemoryRuntime restartedRuntime(config);
    MemoryEvent secondEvent = event;
    secondEvent.content = "I prefer persistent cursor checks for database code";
    if (!restartedRuntime.AppendEvent(secondEvent)) {
        std::cerr << "AppendEvent after restart failed\n";
        return 1;
    }
    auto restartedConsolidationResult = restartedRuntime.Consolidate(consolidateRequest);
    if (!restartedConsolidationResult || restartedConsolidationResult.processedEvents != 1) {
        std::cerr << "persisted consolidation cursor failed across runtime restart\n";
        return 1;
    }

    MemoryContextRequest contextRequest;
    contextRequest.agentId = "agent-1";
    contextRequest.sessionId = "session-1";
    auto restartedContext = restartedRuntime.BuildContext(contextRequest);
    bool foundPersistedPayload = false;
    for (const auto& payload : restartedContext.context.payloadRefs) {
        if (payload.uri == payloadResult.payload.uri) {
            foundPersistedPayload = true;
            break;
        }
    }
    if (!foundPersistedPayload) {
        std::cerr << "persisted payload refs missing after runtime restart\n";
        return 1;
    }

    auto context = runtime.BuildContext(contextRequest);
    if (context.context.memoryText.find("Long-term Summaries") == std::string::npos) {
        std::cerr << "BuildContext missing long-term memory\n";
        return 1;
    }
    if (context.context.messages.empty() || context.context.messages[0].content.find("concise answers") == std::string::npos) {
        std::cerr << "BuildContext missing structured messages\n";
        return 1;
    }
    if (context.context.metadata.contains("citations")) {
        std::cerr << "BuildContext should expose citations only as a structured field\n";
        return 1;
    }
    if (fs::exists(dataPath / "memory" / "history.jsonl")) {
        std::cerr << "AppendEvent should not write legacy history files\n";
        return 1;
    }
    auto messagesOnlyRequest = contextRequest;
    messagesOnlyRequest.includeSections = {"messages"};
    auto messagesOnlyContext = runtime.BuildContext(messagesOnlyRequest);
    if (messagesOnlyContext.context.messages.empty() || !messagesOnlyContext.context.memoryText.empty() ||
        !messagesOnlyContext.context.payloadRefs.empty() || !messagesOnlyContext.context.entities.empty()) {
        std::cerr << "BuildContext include sections failed for messages-only request\n";
        return 1;
    }
    auto payloadsOnlyRequest = contextRequest;
    payloadsOnlyRequest.includeSections = {"payloads"};
    auto payloadsOnlyContext = runtime.BuildContext(payloadsOnlyRequest);
    if (!payloadsOnlyContext.context.messages.empty() || payloadsOnlyContext.context.payloadRefs.empty() ||
        !payloadsOnlyContext.context.entities.empty()) {
        std::cerr << "BuildContext include sections failed for payloads-only request\n";
        return 1;
    }
    for (const auto& payload : payloadsOnlyContext.context.payloadRefs) {
        if (payload.agentId != "agent-1" || payload.sessionId != "session-1" || payload.uri == otherPayloadResult.payload.uri) {
            std::cerr << "BuildContext payload ownership filter failed\n";
            return 1;
        }
    }
    auto payloadQueryRequest = payloadsOnlyRequest;
    payloadQueryRequest.query = "TEST_TOOL tool_result";
    auto payloadQueryContext = runtime.BuildContext(payloadQueryRequest);
    if (payloadQueryContext.context.payloadRefs.empty()) {
        std::cerr << "BuildContext payload token query should match across fields\n";
        return 1;
    }
    payloadQueryRequest.query = "tool-name missing-token";
    auto missingPayloadQueryContext = runtime.BuildContext(payloadQueryRequest);
    if (!missingPayloadQueryContext.context.payloadRefs.empty()) {
        std::cerr << "BuildContext payload token query should require all terms\n";
        return 1;
    }
    auto longTermOnlyRequest = contextRequest;
    longTermOnlyRequest.includeSections = {"long_term"};
    auto longTermOnlyContext = runtime.BuildContext(longTermOnlyRequest);
    if (!longTermOnlyContext.context.messages.empty() || !longTermOnlyContext.context.payloadRefs.empty() ||
        longTermOnlyContext.context.memoryText.find("Long-term") == std::string::npos) {
        std::cerr << "BuildContext include sections failed for long-term-only request\n";
        return 1;
    }
    auto messagesLongTermRequest = contextRequest;
    messagesLongTermRequest.includeSections = {"messages", "long_term"};
    auto messagesLongTermContext = runtime.BuildContext(messagesLongTermRequest);
    if (messagesLongTermContext.context.messages.empty() || !messagesLongTermContext.context.payloadRefs.empty() ||
        messagesLongTermContext.context.memoryText.find("Long-term") == std::string::npos) {
        std::cerr << "BuildContext include sections failed for messages plus long-term request\n";
        return 1;
    }

    auto zeroLimitRequest = contextRequest;
    zeroLimitRequest.metadata["message_limit"] = 0;
    zeroLimitRequest.metadata["payload_limit"] = 0;
    zeroLimitRequest.metadata["long_term_limit"] = 0;
    auto zeroLimitContext = runtime.BuildContext(zeroLimitRequest);
    if (!zeroLimitContext.context.messages.empty() || !zeroLimitContext.context.payloadRefs.empty() ||
        !zeroLimitContext.context.entities.empty() || !zeroLimitContext.context.relations.empty() ||
        zeroLimitContext.context.memoryText.find("Long-term") != std::string::npos) {
        std::cerr << "BuildContext limit zero handling failed\n";
        return 1;
    }

    auto queryContextRequest = contextRequest;
    queryContextRequest.query = "preference";
    auto queryContext = runtime.BuildContext(queryContextRequest);
    if (queryContext.context.memoryText.find("Relevant Long-Term Memory") == std::string::npos ||
        queryContext.context.entities.empty() || queryContext.context.entities[0].id != "entity:preference.user" ||
        queryContext.context.citations.empty()) {
        std::cerr << "BuildContext query structured result failed\n";
        return 1;
    }

    auto builtinModelPath = fs::temp_directory_path() / "agent_memory_cpp_builtin_model_test";
    fs::remove_all(builtinModelPath);
    int builtinModelCalls = 0;
    MemoryConfig builtinModelConfig;
    builtinModelConfig.dataPath = builtinModelPath.string();
    builtinModelConfig.model.enabled = true;
    builtinModelConfig.model.formatType = "openai";
    builtinModelConfig.model.baseUrl = "https://example.com/v1";
    builtinModelConfig.model.modelName = "test-model";

    auto explicitDisablePath = fs::temp_directory_path() / "agent_memory_cpp_disable_model_test";
    fs::remove_all(explicitDisablePath);
    MemoryConfig explicitDisableConfig = builtinModelConfig;
    explicitDisableConfig.dataPath = explicitDisablePath.string();
    int disabledModelCalls = builtinModelCalls;
    BuiltinMemoryRuntime explicitDisableRuntime(explicitDisableConfig);
    MemoryEvent explicitDisableEvent = event;
    explicitDisableEvent.agentId = "agent-disable";
    explicitDisableEvent.sessionId = "session-disable";
    explicitDisableEvent.content = "I prefer explicit model disabling";
    if (!explicitDisableRuntime.AppendEvent(explicitDisableEvent)) {
        std::cerr << "explicit disable append failed\n";
        return 1;
    }
    MemoryConsolidationRequest explicitDisableRequest;
    explicitDisableRequest.agentId = "agent-disable";
    explicitDisableRequest.sessionId = "session-disable";
    auto explicitDisableResult = explicitDisableRuntime.Consolidate(explicitDisableRequest, nullptr);
    if (!explicitDisableResult || !explicitDisableResult.fallbackUsed || builtinModelCalls != disabledModelCalls) {
        std::cerr << "Consolidate(request, nullptr) should disable configured builtin model\n";
        return 1;
    }
    fs::remove_all(explicitDisablePath);

    auto hostModelPath = fs::temp_directory_path() / "agent_memory_cpp_host_model_test";
    fs::remove_all(hostModelPath);
    MemoryConfig hostModelConfig = builtinModelConfig;
    hostModelConfig.dataPath = hostModelPath.string();
    int ignoredBuiltinCalls = builtinModelCalls;
    BuiltinMemoryRuntime hostModelRuntime(hostModelConfig);
    MemoryEvent hostModelEvent = event;
    hostModelEvent.agentId = "agent-host";
    hostModelEvent.sessionId = "session-host";
    hostModelEvent.content = "Please remember host model precedence";
    if (!hostModelRuntime.AppendEvent(hostModelEvent)) {
        std::cerr << "host model append failed\n";
        return 1;
    }
    StaticModelClient hostModel(R"({"topicSummaries":["Host model was used"],"profileSummaries":[],"entities":[{"id":"entity:host.model","entityType":"topic","name":"Host model","summary":"Host model precedence","confidence":0.9}],"relations":[]})");
    MemoryConsolidationRequest hostModelRequest;
    hostModelRequest.agentId = "agent-host";
    hostModelRequest.sessionId = "session-host";
    auto hostModelResult = hostModelRuntime.Consolidate(hostModelRequest, &hostModel);
    if (!hostModelResult || hostModelResult.fallbackUsed || hostModelResult.savedEntities != 1 || hostModel.calls != 1 || builtinModelCalls != ignoredBuiltinCalls) {
        std::cerr << "explicit host model should override configured builtin model\n";
        return 1;
    }
    fs::remove_all(hostModelPath);

    auto stats = runtime.GetStats();
    if (stats.stats.events < 1 || stats.stats.payloads < 1 || stats.stats.summaries < 1) {
        std::cerr << "stats are incomplete\n";
        return 1;
    }

    fs::path concurrentDataPath = fs::temp_directory_path() / "agent_memory_cpp_concurrent_test";
    fs::remove_all(concurrentDataPath);
    MemoryConfig concurrentConfig;
    concurrentConfig.dataPath = concurrentDataPath.string();
    concurrentConfig.enablePayloadOffload = true;
    concurrentConfig.offloadThresholdChars = 4;
    BuiltinMemoryRuntime concurrentRuntime(concurrentConfig);
    std::atomic<bool> ok{true};
    std::mutex refsMutex;
    std::vector<std::string> payloadRefs;
    std::vector<std::thread> threads;
    constexpr int threadCount = 8;
    constexpr int eventsPerThread = 25;
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&concurrentRuntime, &ok, t]() {
            for (int i = 0; i < eventsPerThread; ++i) {
                MemoryEvent concurrentEvent;
                concurrentEvent.type = MemoryEventType::MESSAGE_APPENDED;
                concurrentEvent.agentId = "agent-concurrent";
                concurrentEvent.sessionId = "session-" + std::to_string(t);
                concurrentEvent.role = "user";
                concurrentEvent.content = "concurrent event " + std::to_string(t) + ":" + std::to_string(i);
                if (!concurrentRuntime.AppendEvent(concurrentEvent)) {
                    ok = false;
                }
            }
        });
    }
    threads.emplace_back([&concurrentRuntime, &ok]() {
        for (int i = 0; i < eventsPerThread; ++i) {
            MemoryContextRequest request;
            request.agentId = "agent-concurrent";
            request.sessionId = "session-0";
            auto package = concurrentRuntime.BuildContext(request);
            if (!package.context.metadata.contains("agentId")) {
                ok = false;
            }
        }
    });
    threads.emplace_back([&concurrentRuntime, &ok]() {
        for (int i = 0; i < eventsPerThread; ++i) {
            MemoryConsolidationRequest request;
            request.agentId = "agent-concurrent";
            request.sessionId = "session-0";
            auto result = concurrentRuntime.Consolidate(request);
            if (result.error.HasError()) {
                ok = false;
            }
        }
    });
    threads.emplace_back([&concurrentRuntime, &ok]() {
        for (int i = 0; i < eventsPerThread; ++i) {
            MemorySearchRequest request;
            request.agentId = "agent-concurrent";
            request.sessionId = "session-0";
            request.query = "concurrent";
            request.limit = 5;
            concurrentRuntime.SearchMemory(request);
        }
    });
    threads.emplace_back([&concurrentRuntime, &ok, &refsMutex, &payloadRefs]() {
        for (int i = 0; i < eventsPerThread; ++i) {
            MemoryPayloadWriteRequest request;
            request.agentId = "agent-concurrent";
            request.sessionId = "payload-session";
            request.toolCallId = "payload-" + std::to_string(i);
            request.toolName = "concurrent_payload";
            request.contentType = "tool_result";
            request.content = "payload-content-" + std::to_string(i);
            auto result = concurrentRuntime.WritePayload(request);
            if (!result.offloaded || concurrentRuntime.ReadPayload(result.payload.uri).content != request.content) {
                ok = false;
            }
            std::lock_guard<std::mutex> lock(refsMutex);
            payloadRefs.push_back(result.payload.uri);
        }
    });
    for (auto& thread : threads) {
        thread.join();
    }
    auto concurrentStats = concurrentRuntime.GetStats();
    if (!ok || concurrentStats.stats.events < threadCount * eventsPerThread ||
        static_cast<int>(payloadRefs.size()) != eventsPerThread) {
        std::cerr << "concurrent append/build/search/payload smoke test failed: ok=" << ok
                  << " events=" << concurrentStats.stats.events
                  << " payload_refs=" << payloadRefs.size() << "\n";
        return 1;
    }
    fs::remove_all(concurrentDataPath);

    fs::remove_all(dataPath);
    return 0;
}
