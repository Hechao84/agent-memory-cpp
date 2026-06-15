# Transport 模块设计

## 模块目标

Transport 模块把网络协议请求转换为统一的 `BuiltinMemoryRuntime` 调用，当前包含 RESTful HTTP 和 MCP-over-HTTP 两种传输方式。

## 主要文件

- `src/transport/http/memory_http_server.*`
- `src/transport/mcp/memory_mcp_protocol.*`
- `src/server/server_main.cpp`
- `src/server/server_options.*`
- `src/server/server_common.*`

## HTTP Server 设计

`MemoryHttpServer` 负责注册路由、认证、请求体大小限制和 JSON 解析。

### 路由

| 方法 | 路径 | Runtime 调用 |
| --- | --- | --- |
| `POST` | `/v1/events` | `AppendEvent` |
| `POST` | `/v1/context` | `BuildContext` |
| `POST` | `/v1/payloads` | `WritePayload` |
| `GET` | `/v1/payloads/{path}` | `ReadPayload` |
| `POST` | `/v1/consolidate` | `Consolidate` |
| `POST` | `/v1/search` | `SearchMemory` |
| `GET` | `/v1/stats` | `GetStats` |
| `GET` | `/health` | 健康检查 |
| `POST` | MCP path，默认 `/mcp` | MCP JSON-RPC |

### 响应 envelope

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
    "code": "...",
    "message": "...",
    "details": "",
    "retryable": false
  },
  "schemaVersion": 1
}
```

传输层错误可能返回简化错误，例如未授权或 JSON 解析错误。

### 认证

如果 `server.auth.apiToken` 非空，则除 `/health` 外所有请求必须携带：

```text
Authorization: Bearer <token>
```

认证比较使用常量时间字符串比较。

### 请求大小限制

- REST 请求体限制：`server.http.maxPayloadBytes`。
- MCP 消息限制：`server.mcp.maxMessageBytes`。
- 超限返回 HTTP 413。

## MCP 协议设计

`MemoryMcpProtocol` 支持 JSON-RPC 2.0：

- `initialize`
- `tools/list`
- `tools/call`

服务信息：

- `serverInfo.name = memory-server`
- `serverInfo.version = 0.1.0`
- `protocolVersion = 2024-11-05`

### 工具列表

| 工具 | Runtime 调用 |
| --- | --- |
| `memory_append_event` | `AppendEvent` |
| `memory_build_context` | `BuildContext` |
| `memory_write_payload` | `WritePayload` |
| `memory_read_payload` | `ReadPayload` |
| `memory_consolidate` | `Consolidate` |
| `memory_search` | `SearchMemory` |
| `memory_stats` | `GetStats` |

工具调用结果以 MCP text content 返回，text 内容是项目自身 JSON envelope 的字符串。

## Server 启动设计

`memory-server` 启动流程：

```text
parse cli
  -> load config json
  -> validate options
  -> CreateServerSetup
     -> PrepareDataPath
     -> map server model config to MemoryConfig.model
     -> BuiltinMemoryRuntime
        -> LoadModelClientFromConfig
  -> httplib::Server
  -> MemoryHttpServer::RegisterRoutes
  -> listen(host, port)
```

CLI 参数：

- `--config <path>`
- `--host <ip>`
- `--port <n>`
- `--data <path>`
- `--help`

## 配置结构

主要配置分组：

- `memory`：dataPath、payload offload、token budget。
- `model`：是否启用外部模型、模型协议和参数。
- `server.http`：host、port、超时、线程数、请求体限制。
- `server.mcp`：mode、path、消息大小限制。
- `server.auth`：apiToken。

## 与其他模块关系

```text
HTTP Client / MCP Client
  -> MemoryHttpServer / MemoryMcpProtocol
  -> json_memory_codec
  -> BuiltinMemoryRuntime
  -> Core / Storage / Consolidation / Model
```