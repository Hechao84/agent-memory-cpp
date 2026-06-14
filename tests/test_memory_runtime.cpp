#include <atomic>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "agent_memory/builtin_memory_runtime.h"

namespace fs = std::filesystem;

using namespace agent_memory;

int main()
{
    fs::path dataPath = fs::temp_directory_path() / "agent_memory_cpp_test";
    fs::remove_all(dataPath);

    MemoryConfig config;
    fs::remove_all(dataPath);

    {
        MemoryConfig badConfig;
        badConfig.dataPath = "/dev/null/";
        BuiltinMemoryRuntime badRuntime(badConfig);
        auto badStats = badRuntime.GetStats();
        if (badStats.stats.payloads != 0 || badStats.stats.events != 0) {
            std::cerr << "init failure should produce empty stats\n";
            return 1;
        }
        MemoryEvent badEvent;
        badEvent.type = MemoryEventType::MESSAGE_APPENDED;
        badEvent.agentId = "agent-1";
        badEvent.sessionId = "session-1";
        badEvent.role = "user";
        badEvent.content = "should fail";
        auto failedAppend = badRuntime.AppendEvent(badEvent);
        if (failedAppend || failedAppend.error.code != "store_unavailable") {
            std::cerr << "AppendEvent should fail when store is unavailable\n";
            return 1;
        }
    }

    config.dataPath = dataPath.string();
    config.enablePayloadOffload = true;
    config.offloadThresholdChars = 10;

    BuiltinMemoryRuntime runtime(config);

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
    if (!payloadResult.offloaded || runtime.ReadPayload(payloadResult.payload.uri).content != payloadRequest.content) {
        std::cerr << "payload offload failed\n";
        return 1;
    }
    auto duplicatePayloadResult = runtime.WritePayload(payloadRequest);
    if (!duplicatePayloadResult.offloaded || duplicatePayloadResult.payload.uri == payloadResult.payload.uri) {
        std::cerr << "duplicate payload refs should not collide\n";
        return 1;
    }
    auto badPayloadRead = runtime.ReadPayload("file:///etc/passwd");
    if (badPayloadRead || badPayloadRead.error.code != "payload_read_failed") {
        std::cerr << "payload path traversal should be rejected\n";
        return 1;
    }

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

    auto repeatedConsolidationResult = runtime.Consolidate(consolidateRequest);
    if (repeatedConsolidationResult.succeeded || repeatedConsolidationResult.processedEvents != 0) {
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
    if (afterForceResult.succeeded || afterForceResult.processedEvents != 0) {
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
            if (result.error) {
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
