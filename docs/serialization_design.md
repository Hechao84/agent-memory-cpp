# Serialization 模块设计

## 模块目标

Serialization 模块负责 C++ 领域对象与 JSON 的互相转换，为 RESTful API、MCP 工具和测试提供统一 JSON schema。

## 主要文件

- `src/serialization/json_memory_codec.h`
- `src/serialization/json_memory_codec.cpp`
- `src/serialization/json_helpers.h`

## Schema 版本

当前 JSON schema version 为 `1`，由 `JsonMemorySchemaVersion()` 返回。多数编码函数会在对象中加入：

```json
{
  "schemaVersion": 1
}
```

## Envelope 设计

成功响应：

```json
{
  "ok": true,
  "data": {},
  "schemaVersion": 1
}
```

失败响应：

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

构造函数：

- `SuccessEnvelope(data)`
- `ErrorEnvelope(error)`

## 编解码对象

模块支持以下对象的 JSON 转换：

- `MemoryError`
- `MemoryOperationResult`
- `MemoryMessage`
- `MemoryPayloadRef`
- `MemoryEntity`
- `MemoryRelation`
- `MemoryStats`
- `MemoryContextPackage`
- `MemoryContextRequest`
- `MemoryEvent`
- `MemoryPayloadWriteRequest`
- `MemoryPayloadWriteResult`
- `MemoryPayloadReadResult`（SDK 结果类型；HTTP/MCP 读取响应当前编码为 `{uri, content}` data 对象）
- `MemoryConsolidationRequest`
- `MemoryConsolidationResult`
- `MemorySearchRequest`
- `MemorySearchResult` 数组

当前 `MemoryEvent` JSON 包含 `metadata`，Store 会持久化该字段。C++ `MemoryEvent` 还包含 `storeCursor`，由 Store 读取事件时填充，主要用于 consolidation cursor 跟踪；外部调用方通常不需要设置。`MemoryPayloadRef` JSON 包含 `agentId/sessionId`，用于 payload 上下文隔离。

## 诊断设计

`JsonDecodeDiagnosticsFor` 根据 schema 名称做弱校验：

- JSON 根必须为 object，否则记录 error。
- 字段存在但类型不匹配时记录 warning。
- `schemaVersion` 高于当前版本时记录 warning。
- 未知 schema 名称记录 warning。

诊断结构：

```text
JsonDecodeDiagnostics
  warnings: vector<JsonDecodeIssue>
  errors: vector<JsonDecodeIssue>
```

`Decode*` 函数返回 `false` 表示存在 errors。

## 容错策略

反序列化采用默认值容错：

- 缺失字符串字段默认为空字符串。
- 缺失整数使用结构体默认值，如 `tokenBudget=4096`、`limit=10`。
- 缺失对象字段默认为空 object。
- 缺失数组字段默认为空数组。

## 与其他模块关系

```text
HTTP / MCP
  -> json_memory_codec
  -> MemoryRuntime 数据结构
```

Transport 层不直接解析业务字段，而是通过 codec 转换为领域对象，保证 SDK、HTTP、MCP 对同一数据模型保持一致。