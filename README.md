# agent-memory-cpp

A standalone C++17 agent memory runtime with SDK, HTTP server, and MCP server delivery modes.

## Features

- Event store for agent/session/message/tool events
- Payload offload for large tool results
- SQLite-backed long-term summaries, entities, and relations
- Rule-based consolidation by default
- OpenAI-compatible LLM extraction via `--model-config`
- Optional host-provided `MemoryModelClient` interface for SDK extraction
- REST sidecar server
- MCP stdio server

## Build

```bash
./scripts/build_linux.sh
```

Artifacts are written to:

```text
dist/linux/
  bin/memory-server
  bin/memory-mcp-server
  lib/libagent_memory.so
  include/agent_memory/*
  examples/memory_server/*
```

## SDK Mode

```cpp
#include "agent_memory/builtin_memory_runtime.h"

using namespace agent_memory;

MemoryConfig config;
config.dataPath = "./data";
config.enablePayloadOffload = true;

BuiltinMemoryRuntime runtime(config);
runtime.AppendEvent(event);
auto context = runtime.BuildContext(request);
runtime.Consolidate(consolidationRequest);
```

Main APIs:

```text
AppendEvent(event)
WritePayload(request)
ReadPayload(ref)
BuildContext(request)
Consolidate(request)
SearchMemory(request)
GetStats()
```

## HTTP Mode

```bash
LD_LIBRARY_PATH=./dist/linux/lib ./dist/linux/bin/memory-server --host 127.0.0.1 --port 8090 --data ./data
```

Endpoints:

```text
POST /v1/events
POST /v1/context
POST /v1/payloads
GET  /v1/payloads/{ref}
POST /v1/consolidate
POST /v1/search
GET  /v1/stats
GET  /health
```

Example:

```bash
curl -X POST http://127.0.0.1:8090/v1/events \
  -H 'Content-Type: application/json' \
  -d '{"type":2,"agentId":"agent-1","sessionId":"session-1","role":"user","content":"I prefer concise answers"}'

curl -X POST http://127.0.0.1:8090/v1/consolidate \
  -H 'Content-Type: application/json' \
  -d '{"agentId":"agent-1","sessionId":"session-1","force":true}'

curl -X POST http://127.0.0.1:8090/v1/context \
  -H 'Content-Type: application/json' \
  -d '{"agentId":"agent-1","sessionId":"session-1","query":"answer the user"}'
```

## MCP Mode

```bash
LD_LIBRARY_PATH=./dist/linux/lib ./dist/linux/bin/memory-mcp-server --data ./data
```

Tools:

```text
memory_append_event
memory_build_context
memory_read_payload
memory_consolidate
memory_search
memory_stats
```

## LLM Consolidation

The core runtime exposes `MemoryModelClient` for host integration:

```cpp
class MyMemoryModelClient : public agent_memory::MemoryModelClient
{
public:
    std::string InvokeMemoryExtraction(const std::string& prompt) override;
};
```

Pass it to:

```cpp
runtime.Consolidate(request, &modelClient);
```

Standalone HTTP/MCP binaries support OpenAI-compatible model config:

```bash
cp examples/memory_server/model_config.example.json model_config.local.json
LD_LIBRARY_PATH=./dist/linux/lib ./dist/linux/bin/memory-server --data ./data --model-config ./model_config.local.json
LD_LIBRARY_PATH=./dist/linux/lib ./dist/linux/bin/memory-mcp-server --data ./data --model-config ./model_config.local.json
```

If config loading or model calls fail, consolidation falls back to rule-based extraction.

## Dependencies

- C++17
- CMake >= 3.15
- libcurl
- SQLite3
- nlohmann/json
- cpp-httplib for `memory-server`

The current local build can reuse prebuilt third-party headers/libs from a sibling jiuwen-lite checkout while the repository is being bootstrapped.
