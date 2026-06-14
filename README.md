# agent-memory-cpp

A standalone C++17 agent memory runtime with SDK, RESTful API, and MCP-over-HTTP integration modes.

## Overview

agent-memory-cpp provides local-first memory capabilities for agent applications:

- Agent/session/message/tool event storage
- Large tool-result payload offload
- SQLite-backed long-term summaries, entities, and relations
- Context package construction for agent prompts
- Long-term memory consolidation with rule-based fallback
- Optional OpenAI-compatible or Anthropic-compatible LLM extraction
- SDK, RESTful API, and MCP-over-HTTP delivery modes

## Documentation

Project documentation is maintained under `docs/`:

| Document | Description |
| --- | --- |
| `docs/architecture.md` | Project goals, architecture layers, modules, external interfaces, and module relationships |
| `docs/interfaces.md` | Detailed SDK, RESTful API, MCP Client, parameter, response, and server configuration reference |
| `docs/core_runtime_design.md` | Runtime orchestration, service composition, concurrency, and SDK runtime design |
| `docs/payload_context_design.md` | Payload offload and context package construction design |
| `docs/consolidation_design.md` | Event-to-long-term-memory consolidation design |
| `docs/storage_design.md` | Store abstraction, SQLite schema, indexes, FTS search, and persistence design |
| `docs/model_design.md` | ModelClient, OpenAI-compatible, and Anthropic-compatible model integration design |
| `docs/serialization_design.md` | JSON schema, envelope, codec, and diagnostics design |
| `docs/transport_design.md` | REST, MCP-over-HTTP, auth, request limits, and server startup design |

Start with `docs/architecture.md` for the system design and `docs/interfaces.md` for business integration.

## Build

```bash
./scripts/build_linux.sh
```

Artifacts are written to:

```text
dist/linux/
  bin/memory-server
  lib/libagent_memory.so
  include/agent_memory/*
  examples/memory_server/*
```

## Quick Start: SDK Integration

```cpp
#include "agent_memory/builtin_memory_runtime.h"

using namespace agent_memory;

MemoryConfig config;
config.dataPath = "./data";
config.enablePayloadOffload = true;
config.offloadThresholdChars = 8000;
config.tokenBudget = 4096;

BuiltinMemoryRuntime runtime(config);

MemoryEvent event;
event.type = MemoryEventType::MESSAGE_APPENDED;
event.agentId = "agent-1";
event.sessionId = "session-1";
event.role = "user";
event.content = "I prefer concise answers";

runtime.AppendEvent(event);

MemoryContextRequest contextRequest;
contextRequest.agentId = "agent-1";
contextRequest.sessionId = "session-1";
contextRequest.query = "answer the user";

auto contextResult = runtime.BuildContext(contextRequest);
```

Main SDK APIs:

```text
AppendEvent(event)
WritePayload(request)
ReadPayload(uri)
BuildContext(request)
Consolidate(request)
Consolidate(request, modelClient)
SearchMemory(request)
GetStats()
```

See `docs/interfaces.md` for all SDK structures and fields.

## Quick Start: RESTful API Integration

```bash
cp examples/memory_server/server_config.example.json server_config.local.json
LD_LIBRARY_PATH=./dist/linux/lib ./dist/linux/bin/memory-server --config ./server_config.local.json
```

Write an event:

```bash
curl -X POST http://127.0.0.1:8090/v1/events \
  -H 'Content-Type: application/json' \
  -d '{"type":2,"agentId":"agent-1","sessionId":"session-1","role":"user","content":"I prefer concise answers"}'
```

Build context:

```bash
curl -X POST http://127.0.0.1:8090/v1/context \
  -H 'Content-Type: application/json' \
  -d '{"agentId":"agent-1","sessionId":"session-1","query":"answer the user"}'
```

Endpoints:

```text
POST /v1/events
POST /v1/context
POST /v1/payloads
GET  /v1/payloads/{path}
POST /v1/consolidate
POST /v1/search
GET  /v1/stats
GET  /health
```

HTTP responses use a common envelope:

```json
{ "ok": true, "data": {}, "schemaVersion": 1 }
```

If `server.auth.apiToken` is configured, add `Authorization: Bearer <token>` to API requests except `/health`.

## Quick Start: MCP Client Integration

The same `memory-server` exposes MCP JSON-RPC over HTTP. The default endpoint is `/mcp`.

```bash
curl -X POST http://127.0.0.1:8090/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

Tools:

```text
memory_append_event
memory_build_context
memory_write_payload
memory_read_payload
memory_consolidate
memory_search
memory_stats
```

MCP tool arguments match the REST JSON request models. See `docs/interfaces.md` for details.

## Server Configuration

Example config:

```json
{
  "memory": {
    "dataPath": "./data/memory-server",
    "enablePayloadOffload": true,
    "offloadThreshold": 8000,
    "tokenBudget": 4096
  },
  "model": {
    "enabled": false,
    "strict": false,
    "formatType": "openai",
    "baseUrl": "<your model api base url>",
    "apiKey": "<your model api key>",
    "modelName": "<your model name>",
    "timeoutSeconds": 60,
    "temperature": 0,
    "maxTokens": 4096
  },
  "server": {
    "debugErrors": false,
    "auth": {
      "apiToken": ""
    },
    "http": {
      "host": "127.0.0.1",
      "port": 8090,
      "maxPayloadBytes": 1048576,
      "readTimeoutSeconds": 30,
      "writeTimeoutSeconds": 30,
      "threadCount": 4
    },
    "mcp": {
      "mode": "http",
      "path": "/mcp",
      "maxMessageBytes": 1048576
    }
  }
}
```

For shared or non-local deployments, configure `server.auth.apiToken` and avoid binding to a public address without authentication.

## LLM Consolidation

SDK mode can provide a host model implementation:

```cpp
class MyModelClient : public agent_memory::ModelClient
{
public:
    agent_memory::ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) override;
};

runtime.Consolidate(request, &modelClient);
```

Server mode supports OpenAI-compatible and Anthropic-compatible model configs through the `model` object. If model loading or invocation fails, consolidation falls back to rule-based extraction unless `model.strict` is `true`.

## Test

```bash
ctest --test-dir build-linux --output-on-failure
```

## Dependencies

- C++17
- CMake >= 3.15
- libcurl
- SQLite3 or bundled SQLite source
- nlohmann/json
- cpp-httplib for `memory-server`
