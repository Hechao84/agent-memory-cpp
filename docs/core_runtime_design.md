# Core Runtime 模块设计

## 模块目标

Core Runtime 是项目的运行时编排层，负责把事件、上下文、载荷、长期记忆、检索和统计能力组织成统一的 `MemoryRuntime` 接口。

## 主要文件

- `include/agent_memory/runtime.h`：运行时抽象接口。
- `include/agent_memory/builtin_memory_runtime.h`：内置运行时公开实现。
- `src/core/builtin_memory_runtime.cpp`：运行时方法实现和并发控制。
- `src/core/runtime_services.*`：内部服务装配。
- `src/core/runtime_store_initializer.*`：存储创建与初始化。
- `src/core/runtime_paths.*`：运行时数据路径解析。
- `src/core/context_builder.*`：上下文构建。
- `src/core/payload_service.*`：载荷卸载与读取。

## 对外接口

`MemoryRuntime` 定义以下方法：

| 方法 | 输入 | 输出 | 职责 |
| --- | --- | --- | --- |
| `AppendEvent` | `MemoryEvent` | `MemoryOperationResult` | 写入事件 |
| `BuildContext` | `MemoryContextRequest` | `MemoryContextResult` | 构建上下文包 |
| `WritePayload` | `MemoryPayloadWriteRequest` | `MemoryPayloadWriteResult` | 写入或卸载大载荷 |
| `ReadPayload` | `uri` | `MemoryPayloadReadResult` | 读取卸载载荷 |
| `Consolidate` | `MemoryConsolidationRequest` | `MemoryConsolidationResult` | 触发长期记忆沉淀 |
| `SearchMemory` | `MemorySearchRequest` | `MemorySearchResponse` | 检索长期记忆 |
| `GetStats` | 无 | `MemoryStatsResult` | 查询统计 |

`BuiltinMemoryRuntime` 是当前唯一内置实现。

## 内部结构

`BuiltinMemoryRuntimeImpl` 持有：

- `RuntimeServices services`：Store、PayloadService、ContextBuilder、ConsolidationService、内置 ModelClient 等。
- `mutex`：保护服务指针和少量运行时状态。
- `consolidationMutex`：串行化长期记忆沉淀。
- `lastRuntimeError`：最近运行时错误。

`RuntimeServices` 创建关系：

```text
CreateRuntimeServices
  -> CreateRuntimeStore
  -> LoadModelClientFromConfig(config.model)
  -> PayloadService(config, dataPath, store)
  -> ContextBuilder(config, store)
  -> RuleBasedLongTermMemoryProcessor
  -> MemoryUpdateWriter(store)
  -> ConsolidationService(writer, fallbackProcessor)
```

## 关键流程

### AppendEvent

1. 获取 `MemoryStore`。
2. 调用 `MemoryStore::SaveEvent` 持久化。
3. 保存失败时返回 `event_persist_failed`。

Store 是事件的单一事实来源，Runtime 不再维护事件内存快照。

### BuildContext

1. 获取 `ContextBuilder`。
2. 调用 `ContextBuilder::BuildContext`。
3. `ContextBuilder` 从 Store 按 agent/session 读取事件、payload 和长期记忆。
4. 返回 `MemoryContextPackage`。

### WritePayload

1. 获取 `PayloadService`。
2. 按配置判断是否卸载。
3. 如果卸载成功，将 payload 文件写入本地文件系统，并通过 Store 保存 `MemoryPayloadRef`。

### Consolidate

1. `Consolidate(request)` 从 `RuntimeServices::modelClient` 获取内置模型；`Consolidate(request, model)` 使用显式传入模型，`nullptr` 表示禁用模型。`BuiltinMemoryRuntime::GetModelStatus()` 可查询内置模型配置和加载状态。
2. 读取当前 agent/session 的 consolidation cursor。
3. 读取游标之后的事件。
4. 调用 `ConsolidationService::Consolidate`。
5. 成功后保存新游标。

## 并发设计

- 所有运行时状态访问通过 `mutex` 保护。
- 长期记忆沉淀通过 `consolidationMutex` 串行执行，避免同一运行时内 cursor 竞争。
- Store 自身也有独立锁保护 SQLite 连接。

## 配置

`MemoryConfig` 字段：

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `dataPath` | 空 | 数据目录；服务端会解析成可写绝对路径 |
| `tokenBudget` | `4096` | 默认上下文 token 预算 |
| `offloadThresholdChars` | `8000` | payload 卸载字符阈值 |
| `enablePayloadOffload` | `false` | 是否启用 payload 文件卸载 |
| `model.enabled` | `false` | 是否启用 SDK/runtime 内置模型 |
| `model.formatType` | `openai` | 内置模型协议，`openai` 或 `anthropic` |
| `model.baseUrl` | 空 | 模型服务地址 |
| `model.apiKey` | 空 | 模型 API Key |
| `model.modelName` | 空 | 模型名称 |
| `model.timeoutSeconds` | `60` | 模型请求超时 |
| `model.temperature` | `0` | 模型温度 |
| `model.maxTokens` | `0` | 最大输出 token；Anthropic 未设置时使用 4096 |
| `model.headers` | 空 | 额外 HTTP 请求头 |
| `model.extraParams` | `{}` | 额外请求体参数 |

## 错误处理

所有 SDK 方法返回结果对象，不抛业务异常。错误通过 `MemoryError` 表示，包含 `code`、`message`、`details`、`retryable`。