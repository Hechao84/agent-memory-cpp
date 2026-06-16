# Storage 模块设计

## 模块目标

Storage 模块负责记忆数据的持久化和检索。内部 Store 接口按能力拆分，`MemoryStore` 作为组合接口保留，SQLite 实现为 `MemorySqliteStore`。

## 主要文件

- `src/storage/store.h`：内部存储接口。
- `src/storage/sqlite_store.h`
- `src/storage/sqlite_store.cpp`

## 核心接口

`store.h` 按调用方需要拆出多个小接口：

| 接口 | 主要调用方 | 职责 |
| --- | --- | --- |
| `MemoryEventStore` | Runtime、ContextBuilder | 保存和读取事件 |
| `MemoryPayloadStore` | PayloadService、ContextBuilder | 保存和读取 payload 引用 |
| `MemoryLongTermStore` | Consolidation、ContextBuilder | 长期记忆、cursor、事务写入 |
| `MemorySearchStore` | Runtime、ContextBuilder | 长期记忆搜索 |
| `MemoryStatsStore` | Runtime/Server setup | 统计信息 |
| `MemoryStore` | Composition root、SQLite 实现 | 组合以上所有能力并提供 `Initialize()` |

`MemoryStore` 组合接口仍提供以下能力：

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

FTS 命中使用 SQLite FTS5 `bm25()` 计算相关性，并通过内部 `SearchScoreConfig` 施加 summary/entity/relation 类型权重。不同类型的候选会合并后按 `score` 全局排序，再按 `limit` 截断。LIKE fallback 仅在 FTS 无结果时使用较低固定分数。

`score` 的稳定语义是“越大越相关”；具体算法和权重属于内部实现，后续可继续调优而不改变 SDK/REST/MCP 接口。

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
- `RunInTransaction` 会向回调传入 `MemoryStoreTransaction`，事务内写入必须使用该事务接口，而不是再次调用带锁的 Store 公共写方法。
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

```text
<dataPath>/
  memory_runtime/
    memory.db
    payloads/
      <session>_<toolCall>_<random>.txt
```

## 安全与备份

- 当前实现依赖运行目录和进程 umask 决定 SQLite 数据库、payload 文件和配置文件权限；尚未显式设置 0600 文件权限。生产部署建议将 `dataPath` 放在仅服务进程用户可读写的目录下。
- payload 文件和 SQLite 数据库共同构成一致备份单元。备份/恢复时应同时处理 `memory.db` 和 `payloads/` 目录，避免数据库引用缺失的 payload 文件。
- metadata 保存失败时，PayloadService 会删除已经写入的 payload 文件；进程崩溃遗留的 `.txt.tmp.` 临时文件不会被数据库引用，会在后续 payload 写入时按 TTL 清理。
- 当前没有内置在线备份、加密存储或文件权限修复机制；这些能力应作为部署/运维增强单独设计。