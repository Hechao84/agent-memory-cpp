# Consolidation 模块设计

## 模块目标

Consolidation 模块负责把短期事件流沉淀为长期记忆，包括会话摘要、主题摘要、用户画像、实体和实体关系。

## 主要文件

- `src/consolidation/consolidation_service.*`
- `src/consolidation/consolidation_batch_builder.*`
- `src/consolidation/long_term_memory_processor.h`
- `src/consolidation/rule_based_processor.*`
- `src/consolidation/llm_processor.*`
- `src/consolidation/memory_update_writer.*`

## 核心对象

### LongTermMemoryBatch

长期记忆抽取输入：

- `events`：参与沉淀的事件列表。
- `sourceRefs`：来源引用，通常指向事件游标或事件 ID。

### LongTermMemoryUpdate

长期记忆抽取结果：

- `entities`
- `relations`
- `profileSummaries`
- `topicSummaries`

### LongTermMemoryProcessor

长期记忆抽取策略接口：

```cpp
virtual LongTermMemoryUpdate Process(const LongTermMemoryBatch& batch) = 0;
```

当前实现：

- `RuleBasedLongTermMemoryProcessor`：规则抽取。
- `LlmLongTermMemoryProcessor`：通过 `ModelClient` 调用外部模型抽取；当前由 `ConsolidationService` 在每次调用时临时创建，不作为长期持有的服务对象。

## 处理流程

```text
BuiltinMemoryRuntime::Consolidate
  -> LoadConsolidationCursor
  -> LoadEventsAfterCursor
  -> ConsolidationService::Consolidate
     -> ConsolidationBatchBuilder::Build
     -> LlmLongTermMemoryProcessor(model)
     -> RuleBasedLongTermMemoryProcessor fallback
     -> MemoryUpdateWriter::RunInTransaction
        -> SaveSessionSummary
        -> SaveUpdate
        -> SaveConsolidationCursor
```

## 批处理构造

`ConsolidationBatchBuilder` 根据 `MemoryConsolidationRequest` 和事件列表生成：

- 参与处理的事件批次。
- `sessionId`
- `nextCursor`
- `sessionSummary`
- `sessionSourceRefs`

`maxEvents` 控制单次最大处理事件数；`forceReprocess` 由运行时决定是否忽略历史 cursor。`forceReprocess=false` 时从 Store 保存的 cursor 之后继续处理；`forceReprocess=true` 时从头重跑当前 agent/session 范围内的事件。`maxEvents<=0` 表示不裁剪批次，当前实现会处理加载到的全部事件。

### 排除会话机制

`MemoryConsolidationRequest.excludedSessionIds` 是一个通用的会话 ID 排除集。非空时，`ConsolidationBatchBuilder::Build` 在 `sessionId` 正向匹配过滤之后、事件类型过滤之前，会跳过 `event.sessionId` 出现在排除集中的事件。被排除的事件：

- 仍然由 Store 持久化（审计回溯）。
- 仍然推进 `nextCursor`（cursor 在过滤之前赋值，避免下次重复扫描）。
- 不进入 `batch.events`、不计入 `processedEvents`、不被 LLM/规则处理器看到。

`MemoryEventStore::LoadEventsAfterCursor` 同样接受 `excludedSessionIds` 参数：当 Store 实现是 SQL 后端时，会在 SQL 层加 `AND session_id NOT IN (?, ?, ...)` 动态绑定，避免把注定会被丢弃的行加载进内存。空集时 SQL 与原行为完全一致（向后兼容）。

排除集是**性能优化 + 防御性过滤**的双层保障：Store 层减少 IO，batch builder 层对非 SQL 实现兜底。两者必须同时配置才能保证所有 Store 实现语义一致。

## LLM 与规则回退

处理顺序：

1. 如果 Runtime 提供了内置 `ModelClient`，或调用方显式传入 `ModelClient*`，优先使用 `LlmLongTermMemoryProcessor`。
2. `Consolidate(request)` 使用 `MemoryConfig.model` 配置的内置模型；`Consolidate(request, model)` 只使用显式传入模型。
3. `Consolidate(request, nullptr)` 表示显式禁用模型，只走规则抽取。
4. 如果 LLM 未产生有效 update，且 fallback processor 存在，则使用规则抽取。
5. `fallbackUsed=true` 表示使用了规则回退。

规则处理器当前识别：

- 偏好信号：`i like`、`i prefer`、`i want`、`please`、`always`、`never`。
- 主题信号：project、repository、code、api、test、deploy、database、config、security、ui、frontend、backend、memory 等。

## 写入设计

`MemoryUpdateWriter` 负责把抽取结果写入 Store：

- session summary 始终按批次保存。
- profile/topic summaries 保存为长期摘要。
- entities 保存为长期实体。
- relations 保存为实体关系。
- 写入过程在 Store 事务中执行。
- cursor 与本次长期记忆写入在同一个 Store 事务中保存；事务失败时长期记忆和 cursor 一起回滚，避免数据已推进但 cursor 未推进造成重复处理。

实体去重和替换由 Store/Writer 配合完成，SQLite 实现支持把重复或被替代实体标记为 obsolete。

## 错误处理

- 无事件可处理且 `nextCursor` 为空时返回 `succeeded=true`、`processedEvents=0`、空 `error` 和空 `nextCursor`，表示任务成功完成但没有新工作。
- 如果读取到了事件但全部被批处理过滤掉（例如非 message 事件，或全部命中 `excludedSessionIds`），`processedEvents=0` 但 `nextCursor` 非空；此时仍会在事务中保存 cursor，避免下次重复扫描同一批被过滤事件。
- 写入失败返回 `consolidation_failed`，并尽量传播 Store 层的错误详情。
- 读取 cursor 失败由 Runtime 返回 `cursor_load_failed`。
- 读取事件失败由 Runtime 返回 `events_load_failed`。
- 保存 cursor 失败会让 consolidation 事务回滚，并返回 `cursor_save_failed` 或 Store 层错误。

## 与其他模块关系

- 依赖 Storage：读取事件、写入长期记忆和 cursor。
- 依赖 Model：可选的 LLM 抽取。
- 被 Core Runtime 调用。
- 产物被 ContextBuilder 和 SearchMemory 使用。