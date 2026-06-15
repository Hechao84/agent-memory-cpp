# Payload 与 Context 模块设计

## 模块目标

Payload 模块处理大文本工具结果的卸载、引用和读取。Context 模块根据短期事件、长期记忆和 payload 引用构建可直接供 Agent 使用的上下文包。

## 主要文件

- `src/core/payload_service.*`
- `src/core/context_builder.*`
- `src/core/context_sections.h`
- `include/agent_memory/payload.h`
- `include/agent_memory/context.h`

## PayloadService 设计

### 写入流程

```text
WritePayload(request, eventCount)
  -> 未启用 offload：返回原 content
  -> content 为空：返回原 content
  -> content 长度小于 offloadThresholdChars：返回原 content
  -> 创建 payload 文件
  -> 生成 file:// URI
  -> 保存 MemoryPayloadRef
  -> 返回 replacementContent
```

`replacementContent` 格式：

```text
[memory-ref: file://...]
Tool result offloaded from <toolName>, original chars: <n>.
```

### URI 与路径安全

读取时只支持 `file://` URI，并执行以下检查：

- payload 目录必须可 canonicalize。
- 目标文件必须位于配置的 payload 目录内部。
- 空文件或不可读文件返回读取失败。

### PayloadRef 字段

| 字段 | 说明 |
| --- | --- |
| `agentId` | payload 所属 Agent |
| `sessionId` | payload 所属会话 |
| `uri` | payload 文件 URI |
| `contentType` | 内容类型 |
| `summary` | 摘要 |
| `toolName` | 工具名 |
| `originalChars` | 原始字符数 |
| `metadata` | 扩展元数据 |
| `createdAt` | 创建时间，SQLite 写入时维护 |

## ContextBuilder 设计

### 输入

`MemoryContextRequest`：

- `agentId`
- `sessionId`
- `query`
- `tokenBudget`
- `includeSections`
- `metadata`

### 输出

`MemoryContextPackage`：

- `messages`：最近消息。
- `memoryText`：长期记忆和 payload 概览文本。
- `entities`：相关实体。
- `relations`：相关关系。
- `payloadRefs`：相关 payload 引用。
- `citations`：来源引用。
- `metadata`：构建过程统计。

### includeSections

如果 `includeSections` 为空，默认包含全部内容。

支持的逻辑 section：

- `messages`
- `long_term` / `long_term_memory`
- `payloads` / `payload`

### metadata 控制项

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `message_limit` | `20` | 最近消息数量 |
| `payload_limit` | `20` | payload 引用数量 |
| `long_term_limit` | 按 tokenBudget 推导 | 长期记忆条数 |

长期记忆默认上限按 `tokenBudget / 200` 推导，范围为 1 到 50；如果 tokenBudget 非正，默认 20。

### 长期记忆选择

- 如果 `query` 为空：调用 `LoadLongTermMemory(agentId, limit, sessionId)`。
- 如果 `query` 非空：调用 `SearchLongTermMemory`，再加载长期记忆快照，保留检索命中的实体和关系。

### payload 选择

- 先使用运行时内存中的 payload 快照，并按 `agentId/sessionId` 过滤。
- 再从 Store 中加载当前 `agentId/sessionId` 范围内的最近 payload。
- 使用 `query` 对 uri、toolName、summary、contentType 做大小写不敏感过滤。

## 与其他模块关系

```text
BuiltinMemoryRuntime
  -> PayloadService
     -> filesystem
     -> MemoryStore::SavePayload
  -> ContextBuilder
     -> MemoryStore::LoadRecentEvents
     -> MemoryStore::LoadLongTermMemory
     -> MemoryStore::SearchLongTermMemory
     -> MemoryStore::LoadRecentPayloads
```

## 错误处理

- Payload 写入失败返回 `payload_write_failed`。
- Payload 读取失败返回 `payload_read_failed`。
- ContextBuilder 不可用返回 `context_build_failed`。