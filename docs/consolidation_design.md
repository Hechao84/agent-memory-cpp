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
- `LlmLongTermMemoryProcessor`：通过 `ModelClient` 调用外部模型抽取。

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

`maxEvents` 控制单次最大处理事件数；`forceReprocess` 由运行时决定是否忽略历史 cursor。

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

实体去重和替换由 Store/Writer 配合完成，SQLite 实现支持把重复或被替代实体标记为 obsolete。

## 错误处理

- 无事件可处理时返回默认 result，`succeeded=false` 且无错误。
- 写入失败返回 `consolidation_failed`。
- 保存 cursor 失败由 Runtime 返回 `cursor_save_failed`。

## 与其他模块关系

- 依赖 Storage：读取事件、写入长期记忆和 cursor。
- 依赖 Model：可选的 LLM 抽取。
- 被 Core Runtime 调用。
- 产物被 ContextBuilder 和 SearchMemory 使用。