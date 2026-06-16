# 架构设计

## 项目目标

agent-memory-cpp 是一个独立的 C++17 Agent 记忆运行时，目标是为 Agent 应用提供统一的短期事件记录、长文本载荷卸载、长期记忆沉淀、上下文构建和记忆检索能力。

项目支持三种交付/集成形态：

- SDK 集成：业务进程直接链接 `libagent_memory.so`，通过 `BuiltinMemoryRuntime` 调用记忆能力。
- RESTful API 集成：启动 `memory-server`，通过 HTTP JSON 接口访问同一套运行时能力。
- MCP Client 集成：同一个 `memory-server` 暴露 MCP-over-HTTP JSON-RPC 工具接口，供 MCP Client 调用。

核心设计目标：

- 本地优先：默认使用 SQLite 和本地文件系统保存记忆数据。
- 运行时统一：SDK 使用 `BuiltinMemoryRuntime`，HTTP/MCP Transport 依赖 `MemoryRuntime` 抽象接口并由 server 注入内置实现。
- 可降级：长期记忆抽取可使用外部模型，也可回退到规则抽取。
- 易集成：公共头文件暴露稳定 C++ 数据结构和运行时接口，服务端暴露 JSON 接口。

## 总体架构

```text
业务应用 / MCP Client / HTTP Client
        │
        ├── SDK: BuiltinMemoryRuntime
        │
        └── memory-server
              ├── REST routes: /v1/*
              └── MCP-over-HTTP: /mcp
                    │
                    ▼
              MemoryRuntime
                    │
            BuiltinMemoryRuntime
                    │
      ┌─────────────┼────────────────┐
      ▼             ▼                ▼
 ContextBuilder  PayloadService  ConsolidationService
      │             │                │
      └───────┬─────┴───────┬────────┘
              ▼             ▼
        MemoryStore      ModelClient
              │             │
              ▼             ▼
       MemorySqliteStore  OpenAI/Anthropic/Host model
              │
              ▼
      SQLite DB + payload files
```

## 架构分层

### 1. 对外接入层

职责：把不同接入协议转换为统一运行时调用。

- SDK：`include/agent_memory/*.h` 中的公共类型和 `MemoryRuntime` / `BuiltinMemoryRuntime`。
- REST：`src/transport/http/memory_http_server.cpp` 注册 `/v1/events`、`/v1/context`、`/v1/payloads`、`/v1/consolidate`、`/v1/search`、`/v1/stats`、`/health`。
- MCP：`src/transport/mcp/memory_mcp_protocol.cpp` 支持 `initialize`、`tools/list`、`tools/call`，并映射到记忆工具。
- CLI Server：`src/server/server_main.cpp` 负责读取配置、创建运行时、启动 HTTP/MCP 服务；`src/server/server_cli.*` 负责命令行参数解析。
- Payload query：`src/core/payload_query.*` 负责 context 构建时的 payload 引用过滤。

### 2. 运行时编排层

职责：统一管理核心服务，提供线程安全的运行时入口。

- `MemoryRuntime`：抽象接口，定义所有核心能力。
- `BuiltinMemoryRuntime`：内置实现，持有 `RuntimeServices`，串联事件、上下文、载荷、长期记忆、检索和统计。
- `RuntimeServices`：创建并持有 Store、PayloadService、ContextBuilder、ConsolidationService、LongTermMemoryProcessor 等内部组件。

### 3. 领域服务层

职责：实现记忆业务逻辑。

- ContextBuilder：根据最近消息、长期记忆、检索结果和载荷引用构造上下文包。
- PayloadService：对大文本工具结果做文件卸载、引用生成、读取和安全路径校验。
- ConsolidationService：批量读取事件，生成会话摘要和长期记忆更新。
- LongTermMemoryProcessor：长期记忆抽取接口。
- RuleBasedLongTermMemoryProcessor：默认规则抽取实现。
- LlmLongTermMemoryProcessor：调用 `ModelClient` 生成长期记忆更新。
- MemoryUpdateWriter：把长期记忆更新写入 Store，并维护实体去重/替换等写入规则。

### 4. 存储层

职责：提供持久化抽象和 SQLite 实现。

- `MemoryStore`：内部存储抽象，覆盖事件、载荷引用、摘要、实体、关系、游标、检索和统计。
- `MemorySqliteStore`：SQLite 实现，使用普通表保存结构化数据，使用 FTS5 支持长期记忆检索。
- Payload 文件：大载荷内容保存在 `<dataPath>/memory_runtime/payloads`，SQLite 只保存引用和元数据。

### 5. 模型适配层

职责：把长期记忆抽取请求适配到外部模型服务或宿主模型能力。

- `ModelClient`：SDK 可实现的模型接口。
- `OpenAiModelClient`：OpenAI-compatible chat completions 适配。
- `AnthropicModelClient`：Anthropic messages 适配。
- `ModelClientFactory`：按配置加载模型客户端。
- `ModelHttpClient`：基于 libcurl 的 HTTP JSON 调用工具。

### 6. 序列化层

职责：公共数据结构与 JSON 的互转。

- `json_memory_codec`：事件、上下文、载荷、长期记忆、检索、统计、错误对象的 JSON 编解码。
- JSON 响应 envelope：成功响应为 `{ "ok": true, "data": ... }`，失败响应为 `{ "ok": false, "error": ... }`。
- `schemaVersion` 当前为 `1`。

## 模块划分

| 模块 | 目录/文件 | 职责 |
| --- | --- | --- |
| 公共 SDK | `include/agent_memory` | 对外 C++ 类型、结果结构和运行时接口 |
| Core Runtime | `src/core` | 内置运行时、上下文构建、载荷服务、服务装配 |
| Consolidation | `src/consolidation` | 事件批处理、长期记忆抽取、写入长期记忆 |
| Storage | `src/storage` | Store 抽象和 SQLite 实现 |
| Serialization | `src/serialization` | JSON 编解码和 schema 诊断 |
| Transport HTTP | `src/transport/http` | REST 路由、认证、请求大小限制 |
| Transport MCP | `src/transport/mcp` | MCP JSON-RPC 协议和工具注册 |
| Server | `src/server` | 服务端 CLI、配置加载、运行时创建 |
| Model | `src/model` | 模型配置、HTTP 调用、OpenAI/Anthropic 适配 |
| Utils | `src/utils` | 文件、路径、curl 全局初始化等工具 |

## 核心数据流

### 写入事件

```text
AppendEvent / POST /v1/events / memory_append_event
  -> BuiltinMemoryRuntime::AppendEvent
  -> MemoryStore::SaveEvent
  -> memory_events
```

事件是短期记忆和后续长期记忆沉淀的基础数据。

### 写入/读取大载荷

```text
WritePayload
  -> PayloadService::WritePayload
  -> 小于阈值或未启用卸载：直接返回原文
  -> 大于阈值：写入 payload 文件，保存 MemoryPayloadRef
  -> MemoryStore::SavePayload
```

读取时仅支持 `file://` URI，并检查目标路径必须位于配置的 payload 目录内。

### 构建上下文

```text
BuildContext
  -> ContextBuilder
     -> LoadRecentEvents 生成 messages
     -> SearchLongTermMemory 或 LoadLongTermMemory 生成长期记忆文本
     -> LoadRecentPayloads 生成载荷引用概览
  -> MemoryContextPackage
```

`includeSections` 可控制包含 `messages`、`long_term`、`payloads` 等内容。`metadata.message_limit`、`metadata.payload_limit`、`metadata.long_term_limit` 可调整采样上限。

### 长期记忆沉淀

```text
Consolidate
  -> 读取 consolidation cursor
  -> LoadEventsAfterCursor
  -> ConsolidationBatchBuilder
  -> LlmLongTermMemoryProcessor 或 RuleBasedLongTermMemoryProcessor
  -> MemoryUpdateWriter::SaveSessionSummary / SaveUpdate
  -> SaveConsolidationCursor
```

`Consolidate(request)` 会使用 `MemoryConfig.model` 初始化的 runtime 内置模型；`Consolidate(request, model)` 使用显式传入模型；`Consolidate(request, nullptr)` 表示显式禁用模型。只要有效模型返回有效更新，就使用 LLM 抽取结果；否则使用规则处理器回退。

### 检索长期记忆

```text
SearchMemory
  -> MemoryStore::SearchLongTermMemory
  -> SQLite FTS5 summaries/entities/relations
  -> MemorySearchResponse
```

## 对外接口

### SDK 接口

核心类：

- `MemoryRuntime`
- `BuiltinMemoryRuntime`
- `ModelClient`

核心方法：

- `AppendEvent(const MemoryEvent&)`
- `BuildContext(const MemoryContextRequest&)`
- `WritePayload(const MemoryPayloadWriteRequest&)`
- `ReadPayload(const std::string& uri)`
- `Consolidate(const MemoryConsolidationRequest&)`：使用 runtime 内置模型；未配置时规则抽取
- `Consolidate(const MemoryConsolidationRequest&, ModelClient*)`：使用显式传入模型；传 `nullptr` 表示禁用模型
- `SearchMemory(const MemorySearchRequest&)`
- `GetStats() const`
- `BuiltinMemoryRuntime::GetModelStatus() const`

两个 `Consolidate` 重载的区别：

| 接口 | 模型来源 | 注意事项 |
| --- | --- | --- |
| `Consolidate(request)` | `MemoryConfig.model` 配置的内置模型 | 适合 SDK/Server 默认路径；无模型时规则抽取 |
| `Consolidate(request, &model)` | 调用方显式传入的宿主模型 | 覆盖内置模型，宿主需保证模型对象在调用期间有效 |
| `Consolidate(request, nullptr)` | 无模型 | 显式禁用模型，不会回退使用内置模型 |

### RESTful API

- `POST /v1/events`
- `POST /v1/context`
- `POST /v1/payloads`
- `GET /v1/payloads/{path}`
- `POST /v1/consolidate`
- `POST /v1/search`
- `GET /v1/stats`
- `GET /health`

如果配置 `server.auth.apiToken`，除 `/health` 外请求必须携带 `Authorization: Bearer <token>`。

### MCP 工具

- `memory_append_event`
- `memory_build_context`
- `memory_write_payload`
- `memory_read_payload`
- `memory_consolidate`
- `memory_search`
- `memory_stats`

MCP 工具参数与 REST JSON 请求体基本一致。

## 模块间接口关系

```text
MemoryHttpServer / MemoryMcpProtocol
  -> MemoryRuntime
     -> BuiltinMemoryRuntime
        -> ContextBuilder
           -> MemoryStore
        -> PayloadService
           -> MemoryStore
           -> filesystem
        -> ConsolidationService
           -> ConsolidationBatchBuilder
           -> LlmLongTermMemoryProcessor
              -> ModelClient
           -> RuleBasedLongTermMemoryProcessor
           -> MemoryUpdateWriter
              -> MemoryStore
```

关键内部接口：

- `MemoryRuntime` 是所有接入层依赖的统一业务接口；HTTP/MCP Transport 只编入 server target，不编入 SDK 共享库。
- `MemoryStore` 是所有核心服务依赖的持久化接口。
- `LongTermMemoryProcessor` 是长期记忆抽取策略接口。
- `ModelClient` 是模型能力输入接口，可由 runtime 内置模型配置创建，也可由 SDK 宿主显式传入。
- `json_memory_codec` 是 HTTP/MCP 与 C++ 类型之间的边界层。

## 运行时与并发模型

- `BuiltinMemoryRuntime` 使用短持有 `mutex` 保护服务指针和少量运行时状态；耗时操作在释放该锁后执行。
- `Consolidate` 使用独立的 `consolidationMutex` 串行化同一运行时内的长期记忆沉淀，避免游标并发写入冲突。
- 锁顺序约定：如果一次调用涉及多个锁，先短暂读取 Runtime 服务指针并释放 Runtime `mutex`，再进入 `consolidationMutex`、Store、PayloadService、ContextBuilder 或模型调用；不要在持有 Runtime `mutex` 时执行下层耗时操作。
- `MemorySqliteStore` 使用 `std::recursive_mutex` 保护 SQLite 连接访问。
- `PayloadService` 使用 `std::recursive_mutex` 保护 payload 目录、文件写入/读取和 metadata 写入流程。
- `ContextBuilder` 自身不加锁，依赖 Store/PayloadService 的同步保证；它不维护可变共享状态。
- HTTP/MCP handler 可能被 cpp-httplib 并发调用，线程安全边界由注入的 `MemoryRuntime` 负责。
- 内置 OpenAI/Anthropic `ModelClient` 每次请求创建独立 curl easy handle；注入的自定义 `ModelClient` 或 `JsonPostTransport` 如共享外部状态，需要实现方自行保证线程安全。
- `memory-server` 可通过 `server.http.threadCount` 配置 cpp-httplib 线程池。

## 存储布局

默认数据目录由 `MemoryConfig.dataPath` 或服务端 `memory.dataPath` 指定；内置模型由 `MemoryConfig.model` 或服务端 `model` 配置指定。

```text
<dataPath>/
  memory_runtime/
    memory.db
    payloads/
      <session>_<toolCall>_<random>.txt
```

SQLite 主要表：

- `memory_events`：agent/session 事件流和事件 metadata。
- `memory_payloads`：payload 引用、agent/session 归属和元数据。
- `memory_summaries`：会话、主题、画像等摘要。
- `memory_entities`：长期记忆实体。
- `memory_relations`：实体关系。
- `memory_consolidation_cursors`：长期记忆处理游标。
- `fts_memory_summaries`、`fts_memory_entities`、`fts_memory_relations`：FTS5 检索索引。

## 错误模型

SDK 返回结构都包含 `succeeded` 或可转 bool 的结果对象，以及 `MemoryError`：

- `code`：错误码。
- `message`：错误说明。
- `details`：详细信息。
- `retryable`：是否建议重试。

HTTP/MCP 中业务错误被包装进统一 JSON envelope；HTTP 解析错误、未授权、请求过大等由传输层直接返回。