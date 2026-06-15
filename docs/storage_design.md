# Storage 模块设计

## 模块目标

Storage 模块负责记忆数据的持久化和检索，为运行时提供统一的 `MemoryStore` 抽象，并提供 SQLite 实现 `MemorySqliteStore`。

## 主要文件

- `src/storage/store.h`：内部存储接口。
- `src/storage/sqlite_store.h`
- `src/storage/sqlite_store.cpp`

## 核心接口

`MemoryStore` 提供以下能力：

| 方法 | 职责 |
| --- | --- |
| `Initialize` | 初始化数据库和表结构 |
| `SaveEvent` | 保存事件流 |
| `SavePayload` | 保存 payload 引用 |
| `LoadRecentPayloads` | 读取最近 payload 引用 |
| `SaveSummary` | 保存长期摘要 |
| `SaveEntity` | 保存长期记忆实体 |
| `SaveRelation` | 保存实体关系 |
| `MarkEntityObsolete` | 标记实体被替代 |
| `RunInTransaction` | 事务执行写入逻辑 |
| `LoadConsolidationCursor` | 读取长期记忆处理游标 |
| `SaveConsolidationCursor` | 保存长期记忆处理游标 |
| `LoadEventsAfterCursor` | 按游标读取事件 |
| `LoadRecentEvents` | 读取最近事件 |
| `LoadLongTermMemory` | 读取长期记忆快照 |
| `SearchLongTermMemory` | 检索长期记忆 |
| `GetStoreStats` | 查询统计 |

## SQLite 表设计

| 表 | 说明 |
| --- | --- |
| `memory_events` | 保存 agent/session 事件流和事件 metadata |
| `memory_payloads` | 保存 agent/session 归属、payload URI、类型、摘要和元数据 |
| `memory_summaries` | 保存会话、主题、画像等摘要 |
| `memory_entities` | 保存长期记忆实体 |
| `memory_relations` | 保存实体间关系 |
| `memory_consolidation_cursors` | 保存 consolidation cursor |
| `fts_memory_summaries` | 摘要 FTS5 索引 |
| `fts_memory_entities` | 实体 FTS5 索引 |
| `fts_memory_relations` | 关系 FTS5 索引 |

## 索引设计

SQLite 初始化时创建以下主要索引：

- `idx_events_agent_session_id`：按 agent/session/id 读取事件。
- `idx_payloads_agent_session_created`：按 agent/session/created_at 读取最近 payload。
- `idx_summaries_agent_session_updated`：按 agent/session/updated_at 读取摘要。
- `idx_entities_agent_active`：读取 active 实体。
- `idx_relations_agent_active`、`idx_relations_active`：读取 active 关系。

## 检索设计

`SearchLongTermMemory` 基于 SQLite FTS5，同时覆盖：

- summaries：level、topic、summary。
- entities：type、name、summary。
- relations：from_entity、relation、to_entity。

返回统一的 `MemorySearchResult`：

- `id`
- `type`
- `content`
- `score`
- `sourceRefs`
- `metadata`

## 初始化生命周期

`MemoryStore` 的生命周期需要显式执行：

```text
new MemorySqliteStore
  -> Initialize()
  -> RunInTransaction(...) / Save... / Load...
```

`RunInTransaction` 不会隐式调用 `Initialize()`。如果 Store 尚未初始化，事务会直接返回失败且不会执行回调。Runtime 的组合根负责在创建 Store 后立即初始化。

## 并发与事务

- `MemorySqliteStore` 使用 `std::recursive_mutex` 保护 SQLite 连接。
- 复合写入通过 `RunInTransaction` 执行，但事务入口只负责事务，不负责打开数据库或创建 schema。
- Consolidation 模块通过事务同时保存 session summary 和长期记忆更新。

## 与其他模块关系

```text
BuiltinMemoryRuntime
  -> MemoryStore
ContextBuilder
  -> LoadRecentEvents / LoadLongTermMemory / SearchLongTermMemory / LoadRecentPayloads
PayloadService
  -> SavePayload
ConsolidationService
  -> MemoryUpdateWriter
     -> SaveSummary / SaveEntity / SaveRelation / MarkEntityObsolete / RunInTransaction
```

## 数据文件布局

SQLite 数据库位于运行时数据目录下，payload 原文不直接存入数据库，而是由 PayloadService 写入 `memory_runtime/payloads`，数据库只保存引用。