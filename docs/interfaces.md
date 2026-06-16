# 接口描述

## 通用数据模型

### MemoryConfig

SDK 初始化参数。

| 字段 | 类型 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| `dataPath` | string | 否 | 空 | 数据目录。Server 模式来自 `memory.dataPath` |
| `tokenBudget` | integer | 否 | `4096` | 默认上下文 token 预算 |
| `offloadThresholdChars` | integer | 否 | `8000` | payload 卸载字符阈值 |
| `enablePayloadOffload` | boolean | 否 | `true` | 是否启用 payload 文件卸载；开启后仅当内容长度大于等于 `offloadThresholdChars` 时才写入 payload 文件 |
| `model` | object | 否 | disabled | SDK 内置模型配置；字段见下方内置模型配置，`enabled=false` 表示不启用 |

### MemoryEvent

事件写入请求。

| 字段 | 类型 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| `type` | integer | 是 | `2` | 事件类型枚举值 |
| `agentId` | string | 是 | 空 | Agent 标识 |
| `sessionId` | string | 是 | 空 | 会话标识 |
| `role` | string | 否 | 空 | 消息角色，如 `user`、`assistant`、`tool` |
| `content` | string | 否 | 空 | 消息或事件内容 |
| `toolCallId` | string | 否 | 空 | 工具调用 ID |
| `toolName` | string | 否 | 空 | 工具名称 |
| `payloadRef` | string | 否 | 空 | payload URI 引用 |
| `storeCursor` | string | 否 | 空 | Store 写入后生成的内部游标，用于 consolidation cursor 跟踪；调用方通常不需要设置 |
| `metadata` | object | 否 | `{}` | 扩展元数据，会随事件持久化 |
| `timestamp` | string | 否 | 空 | 时间戳；为空时由存储层写入时间 |

事件类型枚举：

| 值 | 名称 | 说明 |
| --- | --- | --- |
| `0` | `SESSION_STARTED` | 会话开始 |
| `1` | `SESSION_ENDED` | 会话结束 |
| `2` | `MESSAGE_APPENDED` | 追加消息 |
| `3` | `TOOL_CALL_STARTED` | 工具调用开始 |
| `4` | `TOOL_CALL_FINISHED` | 工具调用结束 |
| `5` | `PAYLOAD_OFFLOADED` | payload 已卸载 |
| `6` | `CONSOLIDATION_REQUESTED` | 请求长期记忆沉淀 |
| `7` | `CONSOLIDATION_COMPLETED` | 长期记忆沉淀完成 |

### MemoryContextRequest

构建上下文请求。

| 字段 | 类型 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| `agentId` | string | 是 | 空 | Agent 标识 |
| `sessionId` | string | 是 | 空 | 会话标识 |
| `query` | string | 否 | 空 | 当前查询；用于长期记忆和 payload 过滤 |
| `tokenBudget` | integer | 否 | `4096` | 上下文预算 |
| `includeSections` | string[] | 否 | `[]` | 为空表示包含全部；可用 `messages`、`long_term`、`long_term_memory`、`payloads`、`payload` |
| `metadata` | object | 否 | `{}` | 控制参数 |

`metadata` 支持：

| 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `message_limit` | integer | `20` | 最近消息数量 |
| `payload_limit` | integer | `20` | payload 引用数量 |
| `long_term_limit` | integer | 按 tokenBudget 推导 | 长期记忆条数，显式设置优先 |

### MemoryContextPackage

构建上下文响应中的 `context`。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `messages` | object[] | 最近消息列表 |
| `memoryText` | string | 长期记忆和 payload 概览文本 |
| `entities` | object[] | 相关长期记忆实体 |
| `relations` | object[] | 相关实体关系 |
| `payloadRefs` | object[] | 相关 payload 引用 |
| `citations` | string[] | 来源引用 |
| `metadata` | object | 构建统计，如 message_count、payload_count、entity_count |

### MemoryPayloadWriteRequest

| 字段 | 类型 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| `agentId` | string | 否 | 空 | Agent 标识 |
| `sessionId` | string | 否 | 空 | 会话标识 |
| `content` | string | 是 | 空 | payload 原文 |
| `contentType` | string | 否 | 空 | 内容类型，如 `text/plain`、`application/json` |
| `toolCallId` | string | 否 | 空 | 工具调用 ID |
| `toolName` | string | 否 | 空 | 工具名称 |
| `metadata` | object | 否 | `{}` | 扩展元数据 |

### MemoryPayloadWriteResult

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `succeeded` | boolean | 是否成功 |
| `offloaded` | boolean | 是否写入文件卸载 |
| `replacementContent` | string | 可替换到事件中的内容 |
| `payload` | object | payload 引用 |
| `error` | object | 错误对象 |

### MemoryPayloadReadResult

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `succeeded` | boolean | 是否成功读取 |
| `content` | string | payload 原文；仅成功时有值 |
| `error` | object | 错误对象 |

HTTP/MCP 读取 payload 时当前响应 data 为 `{ "uri": "...", "content": "..." }`，SDK 返回 `MemoryPayloadReadResult`。

### MemoryPayloadRef

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `agentId` | string | payload 所属 Agent |
| `sessionId` | string | payload 所属会话 |
| `uri` | string | `file://` URI |
| `contentType` | string | 内容类型 |
| `summary` | string | 摘要 |
| `toolName` | string | 工具名称 |
| `originalChars` | integer | 原始字符数 |
| `metadata` | object | 扩展元数据 |
| `createdAt` | string | 创建时间 |

### MemoryConsolidationRequest

`forceReprocess=true` 会忽略保存的 cursor 从头重跑当前 agent/session 范围内事件；`maxEvents<=0` 表示不裁剪批次，处理加载到的全部事件。

| 字段 | 类型 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| `agentId` | string | 是 | 空 | Agent 标识 |
| `sessionId` | string | 否 | 空 | 会话标识；为空时处理该 agent 下匹配逻辑中的默认会话范围 |
| `maxEvents` | integer | 否 | `100` | 单次最多处理事件数 |
| `forceReprocess` | boolean | 否 | `false` | 是否忽略历史 cursor 重跑 |
| `metadata` | object | 否 | `{}` | 扩展元数据 |

### MemoryConsolidationResult

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `succeeded` | boolean | 是否成功写入 |
| `fallbackUsed` | boolean | 是否使用规则回退 |
| `processedEvents` | integer | 已处理事件数 |
| `savedSummaries` | integer | 保存摘要数 |
| `savedEntities` | integer | 保存实体数 |
| `savedRelations` | integer | 保存关系数 |
| `nextCursor` | string | 下一次处理游标 |
| `sessionId` | string | 实际会话 ID |
| `error` | object | 错误对象 |

### MemorySearchRequest

| 字段 | 类型 | 必填 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| `agentId` | string | 否 | 空 | Agent 标识 |
| `sessionId` | string | 否 | 空 | 会话标识 |
| `query` | string | 是 | 空 | 检索词 |
| `limit` | integer | 否 | `10` | 最大返回数 |
| `includeSections` | string[] | 否 | `[]` | 预留过滤字段 |
| `metadata` | object | 否 | `{}` | 扩展元数据 |

### MemorySearchResult

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | string | 结果 ID |
| `type` | string | `summary`、`entity`、`relation` 等 |
| `content` | string | 结果内容 |
| `score` | number | 相关性分数，越大越相关；具体计算算法属于实现细节，可能随版本优化 |
| `sourceRefs` | string[] | 来源引用 |
| `metadata` | object | 扩展元数据 |

### MemoryStats

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `events` | integer | 事件数 |
| `payloads` | integer | payload 引用数 |
| `summaries` | integer | 摘要数 |
| `entities` | integer | 实体数 |
| `relations` | integer | 关系数 |
| `metadata` | object | 扩展统计 |

### MemoryError

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `code` | string | 错误码 |
| `message` | string | 错误说明 |
| `details` | string | 详细信息 |
| `retryable` | boolean | 是否建议重试 |

`MemoryError::operator bool()` 在存在错误码时返回 `true`；这与 Result 类型的 `operator bool()`（成功时为 `true`）语义相反。

### MemoryModelStatus

`BuiltinMemoryRuntime::GetModelStatus()` 返回 runtime 内置模型状态；不描述单次 `Consolidate(request, model)` 传入的宿主模型。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `configured` | boolean | 是否配置了内置模型 |
| `available` | boolean | 内置模型是否可用 |
| `formatType` | string | 模型协议，如 `openai` 或 `anthropic` |
| `modelName` | string | 模型名称 |
| `error` | string | 加载或校验失败原因 |

### 错误码目录

| 错误码 | 典型来源 | 含义 |
| --- | --- | --- |
| `store_unavailable` | Runtime / Store | SQLite Store 未初始化或不可用 |
| `sqlite_error` | Store | SQLite prepare/step/exec/commit 等操作失败 |
| `transaction_failed` | Store | 事务回调返回失败但未提供具体错误 |
| `event_persist_failed` | Runtime | 事件持久化失败 |
| `cursor_load_failed` | Runtime consolidation | 读取 consolidation cursor 失败 |
| `events_load_failed` | Runtime consolidation | 读取待处理事件失败 |
| `cursor_save_failed` | Runtime consolidation | 保存 consolidation cursor 失败 |
| `consolidation_failed` | Consolidation | 长期记忆写入失败 |
| `payload_write_failed` | PayloadService | payload 文件或 metadata 写入失败 |
| `payload_read_failed` | PayloadService | payload URI 不支持、越界、文件缺失或不可读 |
| `context_build_failed` | Runtime | ContextBuilder 不可用 |
| `search_unavailable` | Runtime | Search Store 不可用 |
| `stats_unavailable` | Runtime | Stats Store 不可用 |
| `invalid_config` | Model | 模型配置无效 |
| `http_error` | Model | 上游模型 HTTP 请求失败 |
| `parse_error` | Model | 上游模型响应无法解析为有效记忆更新 |

## SDK 集成

### 依赖

- C++17
- `libagent_memory.so`
- 公共头文件 `include/agent_memory/*`
- nlohmann/json 头文件

### 初始化

```cpp
#include "agent_memory/builtin_memory_runtime.h"

agent_memory::MemoryConfig config;
config.dataPath = "./data";
config.enablePayloadOffload = true;
config.offloadThresholdChars = 8000;
config.tokenBudget = 4096;
config.model.enabled = true;
config.model.formatType = "openai";
config.model.baseUrl = "https://example.com/v1";
config.model.apiKey = "your-api-key";
config.model.modelName = "your-model";

agent_memory::BuiltinMemoryRuntime runtime(config);
```

### 写入消息事件

```cpp
agent_memory::MemoryEvent event;
event.type = agent_memory::MemoryEventType::MESSAGE_APPENDED;
event.agentId = "agent-1";
event.sessionId = "session-1";
event.role = "user";
event.content = "I prefer concise answers";

auto result = runtime.AppendEvent(event);
if (!result) {
    // result.error.code / message
}
```

### 写入大载荷

```cpp
agent_memory::MemoryPayloadWriteRequest request;
request.agentId = "agent-1";
request.sessionId = "session-1";
request.toolCallId = "tool-1";
request.toolName = "search";
request.contentType = "application/json";
request.content = largeText;

auto result = runtime.WritePayload(request);
if (result && result.offloaded) {
    // result.payload.uri
    // result.replacementContent 可写回事件 content
}
```

### 构建上下文

```cpp
agent_memory::MemoryContextRequest request;
request.agentId = "agent-1";
request.sessionId = "session-1";
request.query = "answer the user";
request.tokenBudget = 4096;
request.includeSections = {"messages", "long_term", "payloads"};
request.metadata["message_limit"] = 20;
request.metadata["long_term_limit"] = 10;

auto result = runtime.BuildContext(request);
if (result) {
    auto messages = result.context.messages;
    auto memoryText = result.context.memoryText;
}
```

### 触发长期记忆沉淀

```cpp
agent_memory::MemoryConsolidationRequest request;
request.agentId = "agent-1";
request.sessionId = "session-1";
request.maxEvents = 100;
request.forceReprocess = false;

auto result = runtime.Consolidate(request);
```

`Consolidate(request)` 会优先使用 `MemoryConfig.model` 中配置的内置模型；如果未配置、配置不可用或模型未产生有效更新，则使用规则抽取。

### 使用宿主模型

```cpp
class MyModelClient : public agent_memory::ModelClient
{
public:
    agent_memory::ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) override
    {
        agent_memory::ModelInvokeResult result;
        result.text = "{\"entities\":[],\"relations\":[],\"profileSummaries\":[],\"topicSummaries\":[]}";
        return result;
    }
};

MyModelClient model;
auto result = runtime.Consolidate(request, &model);
```

### 模型状态查询

```cpp
auto status = runtime.GetModelStatus();
```

`BuiltinMemoryRuntime::GetModelStatus()` 返回 `configured`、`available`、`formatType`、`modelName` 和 `error`，用于 SDK 调用方判断 `MemoryConfig.model` 是否已成功初始化。该接口只描述 runtime 配置的内置模型，不描述单次 `Consolidate(request, &model)` 传入的宿主模型。

### Consolidate 重载区别和注意事项

| 调用方式 | 模型来源 | 行为 |
| --- | --- | --- |
| `Consolidate(request)` | `MemoryConfig.model` | 优先使用 runtime 内置模型；未配置或不可用时规则抽取 |
| `Consolidate(request, &model)` | 显式传入的宿主模型 | 只使用传入模型，优先级高于内置模型 |
| `Consolidate(request, nullptr)` | 无 | 显式禁用模型，只走规则抽取 |

注意事项：

- 不要把 `Consolidate(request, nullptr)` 理解为“使用内置模型”；它表示本次调用强制不用模型。
- 宿主传入的 `ModelClient*` 生命周期必须覆盖整个调用过程。
- 如果多个线程并发调用带同一个宿主 `ModelClient*` 的重载，宿主模型实现需要自行保证线程安全，或由调用方串行化。
- LLM 返回无效 JSON、空更新或调用失败时，Consolidation 会回退到规则抽取并设置 `fallbackUsed=true`。

### 检索记忆

```cpp
agent_memory::MemorySearchRequest request;
request.agentId = "agent-1";
request.sessionId = "session-1";
request.query = "preference";
request.limit = 10;

auto result = runtime.SearchMemory(request);
```

### 查询统计

```cpp
auto result = runtime.GetStats();
if (result) {
    int events = result.stats.events;
}
```

## RESTful API 集成

### 启动服务

```bash
LD_LIBRARY_PATH=./dist/linux/lib ./dist/linux/bin/memory-server --config ./server_config.local.json
```

### 认证

如果配置了 `server.auth.apiToken`，除 `/health` 外所有请求都需要：

```text
Authorization: Bearer <token>
```

### 通用响应

成功：

```json
{
  "ok": true,
  "data": {},
  "schemaVersion": 1
}
```

失败：

```json
{
  "ok": false,
  "error": {
    "code": "event_persist_failed",
    "message": "failed to persist memory event",
    "details": "",
    "retryable": false
  },
  "schemaVersion": 1
}
```

### POST /v1/events

写入事件。

请求体：`MemoryEvent`。

示例：

```json
{
  "type": 2,
  "agentId": "agent-1",
  "sessionId": "session-1",
  "role": "user",
  "content": "I prefer concise answers",
  "metadata": {}
}
```

成功响应 `data`：

```json
{
  "succeeded": true
}
```

### POST /v1/context

构建上下文。

请求体：`MemoryContextRequest`。

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "query": "answer the user",
  "tokenBudget": 4096,
  "includeSections": ["messages", "long_term", "payloads"],
  "metadata": {
    "message_limit": 20,
    "long_term_limit": 10,
    "payload_limit": 5
  }
}
```

成功响应 `data`：

```json
{
  "context": {
    "messages": [],
    "memoryText": "...",
    "entities": [],
    "relations": [],
    "payloadRefs": [],
    "citations": [],
    "metadata": {},
    "schemaVersion": 1
  }
}
```

### POST /v1/payloads

写入或卸载 payload。

请求体：`MemoryPayloadWriteRequest`。

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "content": "large tool result...",
  "contentType": "text/plain",
  "toolCallId": "tool-1",
  "toolName": "search",
  "metadata": {}
}
```

成功响应 `data`：`MemoryPayloadWriteResult`。

### GET /v1/payloads/{path}

读取 payload 内容。

服务端会把 `{path}` 拼成 `file://{path}` 后读取，因此 `{path}` 必须对应服务端 payload 目录下的实际路径。

成功响应 `data`：

```json
{
  "uri": "file:///.../payload.txt",
  "content": "payload content"
}
```

### POST /v1/consolidate

触发长期记忆沉淀。

请求体：`MemoryConsolidationRequest`。

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "maxEvents": 100,
  "forceReprocess": true,
  "metadata": {}
}
```

成功响应 `data`：`MemoryConsolidationResult`。

### POST /v1/search

检索长期记忆。

请求体：`MemorySearchRequest`。

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "query": "preference",
  "limit": 10,
  "includeSections": [],
  "metadata": {}
}
```

成功响应 `data`：

```json
{
  "ok": true,
  "results": [
    {
      "id": "entity:preference.user",
      "type": "entity",
      "content": "User expressed preferences in session",
      "score": 0.0,
      "sourceRefs": [],
      "metadata": {}
    }
  ],
  "schemaVersion": 1
}
```

### GET /v1/stats

查询统计。

成功响应 `data`：

```json
{
  "stats": {
    "events": 1,
    "payloads": 0,
    "summaries": 1,
    "entities": 1,
    "relations": 0,
    "metadata": {},
    "schemaVersion": 1
  }
}
```

### GET /health

健康检查。

响应：

```json
{
  "status": "ok"
}
```

## MCP Client 集成

### Endpoint

默认 MCP-over-HTTP endpoint：

```text
POST /mcp
```

可通过 `server.mcp.path` 修改。

### initialize

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {}
}
```

响应：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "serverInfo": {
      "name": "memory-server",
      "version": "0.1.0"
    },
    "capabilities": {
      "tools": {}
    }
  }
}
```

### tools/list

请求：

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/list"
}
```

返回工具：

- `memory_append_event`
- `memory_build_context`
- `memory_write_payload`
- `memory_read_payload`
- `memory_consolidate`
- `memory_search`
- `memory_stats`

### tools/call 通用格式

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "memory_append_event",
    "arguments": {}
  }
}
```

工具响应：

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"ok\":true,\"data\":{...},\"schemaVersion\":1}"
      }
    ]
  }
}
```

`text` 字段是字符串形式的 JSON envelope，业务方需要再解析一次。

### memory_append_event

参数同 `MemoryEvent`。

必填：

- `type`
- `agentId`
- `sessionId`

示例：

```json
{
  "type": 2,
  "agentId": "agent-1",
  "sessionId": "session-1",
  "role": "user",
  "content": "I prefer concise answers"
}
```

### memory_build_context

参数同 `MemoryContextRequest`。

必填：

- `agentId`
- `sessionId`

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "query": "answer the user",
  "tokenBudget": 4096,
  "includeSections": ["messages", "long_term"]
}
```

### memory_write_payload

参数同 `MemoryPayloadWriteRequest`。

必填：

- `content`

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "content": "large tool result...",
  "contentType": "text/plain",
  "toolCallId": "tool-1",
  "toolName": "search"
}
```

### memory_read_payload

参数：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `uri` | string | 是 | payload `file://` URI |

示例：

```json
{
  "uri": "file:///.../payload.txt"
}
```

### memory_consolidate

参数同 `MemoryConsolidationRequest`。

必填：

- `agentId`

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "maxEvents": 100,
  "forceReprocess": false
}
```

### memory_search

参数同 `MemorySearchRequest`。

必填：

- `query`

示例：

```json
{
  "agentId": "agent-1",
  "sessionId": "session-1",
  "query": "preference",
  "limit": 10
}
```

### memory_stats

无参数。

示例：

```json
{}
```

## Server 配置接口

示例配置：

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

### 配置字段

| 路径 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `memory.dataPath` | string | `./data` | 数据目录 |
| `memory.enablePayloadOffload` | boolean | `true` | 是否启用 payload 卸载；与 SDK 默认一致，超过阈值才 offload |
| `memory.offloadThreshold` | integer | `8000` | 卸载阈值 |
| `memory.tokenBudget` | integer | `4096` | 默认上下文预算 |
| `model.enabled` | boolean | `false` | 是否启用模型 |
| `model.strict` | boolean | `false` | 模型配置失败时是否启动失败 |
| `model.formatType` | string | `openai` | 模型协议，`openai` 或 `anthropic` |
| `server.debugErrors` | boolean | `false` | 是否向客户端返回异常详情 |
| `server.auth.apiToken` | string | 空 | Bearer Token，空表示不启用认证 |
| `server.http.host` | string | `127.0.0.1` | HTTP 监听地址 |
| `server.http.port` | integer | `8090` | HTTP 端口 |
| `server.http.maxPayloadBytes` | integer | `1048576` | REST 最大请求体 |
| `server.http.readTimeoutSeconds` | integer | `0` | 读超时，0 表示默认 |
| `server.http.writeTimeoutSeconds` | integer | `0` | 写超时，0 表示默认 |
| `server.http.threadCount` | integer | `0` | 线程池大小，0 表示 httplib 默认 |
| `server.mcp.mode` | string | `http` | 当前支持 `http` |
| `server.mcp.path` | string | `/mcp` | MCP endpoint |
| `server.mcp.maxMessageBytes` | integer | `1048576` | MCP 最大消息体 |