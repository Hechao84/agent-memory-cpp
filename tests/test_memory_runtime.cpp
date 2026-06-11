#include <filesystem>
#include <iostream>
#include <string>

#include "agent_memory/builtin_memory_runtime.h"

namespace fs = std::filesystem;

using namespace agent_memory;

int main()
{
    fs::path dataPath = fs::temp_directory_path() / "agent_memory_cpp_test";
    fs::remove_all(dataPath);

    MemoryConfig config;
    config.dataPath = dataPath.string();
    config.enablePayloadOffload = true;
    config.offloadToolResultChars = 10;

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
    if (!payloadResult.offloaded || runtime.ReadPayload(payloadResult.payload.ref) != payloadRequest.content) {
        std::cerr << "payload offload failed\n";
        return 1;
    }

    MemoryConsolidationRequest consolidateRequest;
    consolidateRequest.agentId = "agent-1";
    consolidateRequest.sessionId = "session-1";
    consolidateRequest.force = true;
    if (!runtime.Consolidate(consolidateRequest)) {
        std::cerr << "Consolidate failed\n";
        return 1;
    }

    MemoryContextRequest contextRequest;
    contextRequest.agentId = "agent-1";
    contextRequest.sessionId = "session-1";
    auto context = runtime.BuildContext(contextRequest);
    if (context.memoryText.find("Long-term Summaries") == std::string::npos) {
        std::cerr << "BuildContext missing long-term memory\n";
        return 1;
    }

    auto stats = runtime.GetStats();
    if (stats.events < 1 || stats.payloads < 1 || stats.summaries < 1) {
        std::cerr << "stats are incomplete\n";
        return 1;
    }

    fs::remove_all(dataPath);
    return 0;
}
