# Agent Memory TODO

This is the single source of truth for project TODOs. Do not maintain module-local TODO files.

## Current architecture

- SDK integration uses `BuiltinMemoryRuntime` directly in the application process.
- Server integration uses `memory-server`, which exposes HTTP and MCP endpoints backed by `BuiltinMemoryRuntime`.
- Remote-client runtime support is intentionally not part of this project.

## P2: Re-evaluate public SDK nlohmann/json dependency

- `include/agent_memory/config.h` currently includes `<nlohmann/json.hpp>` because `MemoryModelConfig::extraParams` uses `nlohmann::json` and `MemoryConfig` owns `MemoryModelConfig model` by value.
- Do not treat "split `MemoryModelConfig` into `model_config.h`" as a sufficient fix: `config.h` would still need the complete `MemoryModelConfig` definition for the value member, so the nlohmann dependency would still propagate.
- Real fixes require a public API decision, such as changing `extraParams` to a JSON string / string map / SDK-owned lightweight value type, or changing `MemoryConfig::model` to a pimpl/optional-owned type that can be forward declared.
- Current decision: keep the API unchanged for now. This is an API cleanup / compile-time dependency issue, not a runtime correctness, security, or data-loss issue.
- **Trigger condition**: revisit during an SDK v2.0 breaking refactor, or if SDK users report nlohmann availability / compile-time cost problems.
- **Origin**: Review round 1 item 4d; revised by review round 2 item #5. See `docs/review/round2/review_reply.md` and `docs/review/round2/review_reply_confirm.md`.

## P1: Deferred consolidation follow-ups

### LLM/rule-based merge strategy

- Decide whether to support partial fallback.
- Define merge rules for LLM summaries plus rule-based entities/relations.
- Add tests for partial LLM results.

### Graph query helpers

- Add `LoadEntityById` / `LoadRelationsFrom` query methods to `MemoryStore` if structured graph traversal requires them.

### SQLite schema migration tests

- Add SQLite schema migration tests.

### SourceRefs precision

- Track the exact event ref that triggered each topic extraction.
- Track the exact event ref that triggered each preference extraction.
- Attach only relevant source refs to generated entities and relations.

### Stable public source references

- Introduce stable public event IDs.
- Replace cursor-based refs with durable `event://<id>` refs.
- Add persistence tests for stable source refs.

## P2: Re-evaluate `Consolidate(request, ModelClient*)` on `MemoryRuntime`

- The `ModelClient*` overload currently remains on the abstract `MemoryRuntime` interface.
- **Trigger condition**: Must re-evaluate when either of these occurs:
  1. A second `MemoryRuntime` implementation is introduced that does not need host model override.
  2. SDK enters a v2.0 breaking refactor cycle.
- **Evaluation criteria**: Does the new runtime implementation need host model override? Should the abstract interface stay pure (data operations only)?
- **Possible actions**: Move `Consolidate(request, ModelClient*)` to `BuiltinMemoryRuntime` only, or add `overrideModel` to `MemoryConsolidationRequest` as a non-serializable SDK-only field.
- **Origin**: Review round 1, item 4b. See `docs/review/round1/review_reply.md`.

## P2: Memory retention and forgetting policy

- Prefer automatic forgetting over ordinary business-facing Delete/Clear APIs.
- Define retention quotas by event count, payload size, age, token budget, agent, and session.
- Define payload garbage collection based on age, size, and reference activity.
- Define long-term memory decay/merge/obsolete rules using confidence, update time, source refs, and access activity.
- Keep explicit delete/reset APIs as future admin or compliance operations, not the primary capacity-control mechanism.
- **Next step**: Produce a design document for the forgetting/retention strategy and review it before implementing. Define triggering mechanism (time-driven vs event-driven), interaction with `RunInTransaction`, and per-data-type rules.
- **Origin**: Review round 1, item 14. See `docs/review/round1/review_reply.md`.

## P2: Eliminate public SDK nlohmann/json dependency (full scope)

- This is the broader follow-up to P1 (split `MemoryModelConfig`).
- Audit all public headers in `include/agent_memory/` that expose `nlohmann::json`:
  - `MemoryEvent::metadata`
  - `MemoryPayloadRef::metadata`
  - `MemoryContextRequest::metadata`
  - `MemoryEntity::metadata`
  - `MemoryRelation::metadata`
  - `MemorySearchResult::metadata`
  - `MemoryStats::metadata`
- Decide on a unified approach: lightweight SDK value type, JSON string wrapper, or SDK-owned JSON value.
- This should be a single coordinated API design and migration, not piecemeal field changes.
- **Trigger condition**: When SDK needs to be deployable in environments where nlohmann/json is not available or its compile-time cost is unacceptable.
- **Origin**: Review round 1, item 4d. See `docs/review/round1/review_reply.md`.

## P2: Storage follow-ups

- Evaluate stronger payload consistency options: SQLite BLOB storage for same-transaction content+metadata commits, payload checksum validation, idempotency keys for retry-safe writes, and documented backup/restore strategy covering both SQLite and payload files.
- Add vector/embedding search support for semantic memory search.
- Re-evaluate `RunInTransaction` locking after consolidation transaction semantics are finalized.
- Consider explicit read-through/LRU caching in `ContextBuilder` or Store after profiling; keep Store as the source of truth and use cache only as an acceleration layer.
- Evolve payload query parsing behind `PayloadQuery`: tokenizer improvements, synonym expansion, model-assisted query rewrite, scoring/ranking, and optional payload FTS should not change `MemoryContextRequest.query`.

## P3: Concurrency and scalability

- Define the concurrency model for SDK integration, HTTP server, and MCP server modes.
- Support multiple sessions accessing memory concurrently in the same process.
- Audit runtime, storage, consolidation, payload, and model clients for thread safety.
- Clarify which components own synchronization: runtime-level locks, store-level locks, or synchronized facades.
- Avoid long-held locks around remaining filesystem I/O and payload operations.
- Add higher-volume stress tests for concurrent memory operations.
- Validate behavior under higher server and MCP server request concurrency.
- Document expected consistency guarantees across sessions and agents.

## Completed

- [x] Round 1 review: Runtime no longer maintains events/payloads in-memory snapshots; Store is the single source of truth. (`de37f23`)
- [x] Round 1 review: Runtime mutex narrowed to short-held pointer reads; removed `lastRuntimeError` cross-lock writes. (`acbf0b8`)
- [x] Round 1 review: Transaction callbacks use `MemoryStoreTransaction` to avoid calling locked public methods inside transactions. (`cf810cc`)
- [x] Round 1 review: `Consolidate(request, nullptr)` semantics documented and enforced as pure virtual. (`6f1cdd3`)
- [x] Round 1 review: Runtime model status query added via `GetModelStatus()`. (`b7be493`)
- [x] Round 1 review: Unified model JSON config parsing; eliminated duplicate functions in `server_common.cpp`. (`6f1cdd3`)
- [x] Round 1 review: Transport layer depends on `MemoryRuntime` abstract interface instead of `BuiltinMemoryRuntime`. (`01c89dd`)
- [x] Round 1 review: Store initialization errors captured and propagated; all methods return structured error results when Store unavailable. (`2f9a075`)
- [x] Round 1 review: `MemoryStore` split into 5 capability sub-interfaces. (`115c836`)
- [x] Round 1 review: `RunInTransaction` no longer lazily initializes Store; returns false when uninitialized. (`90b8ea7`)
- [x] Round 1 review: Search score uses FTS5 `bm25()` relevance with type weights; fallback has lower fixed scores with `scoreSource` metadata. (`749961d`)
- [x] Round 1 review: Transport sources moved out of shared library into server executable only. (`82b6ce2`)
- [x] Round 1 review: Model HTTP transport changed from global singleton to instance-scoped injection via `ModelHttpClient`. (`30f3df3`)
- [x] Round 1 review: Payload file names use random unique values instead of `steady_clock`. (`de37f23`)
- [x] Round 1 review: Payload query extracted as `PayloadQuery` module with whitespace tokenization and AND matching. (`da35704`)
- [x] Round 1 review: Event metadata persisted to SQLite `metadata_json` column. (`a9938d3`)
- [x] Round 1 review: Payload agent/session fields persisted and used for filtering. (`7dec19c`)
- [x] Add internal synchronization to SQLite store operations.
- [x] Remove legacy memory file compatibility.
- [x] Remove remote-client `HttpMemoryRuntime` and related remote client config fields.
- [x] Add a smoke test for concurrent append/build/search/payload/consolidate behavior.
- [x] Remove module-local TODO tracking in favor of this root TODO.
- [x] Complete initial Public API naming cleanup.
- [x] Replace runtime metadata error strings with structured result/error wrappers and endpoint envelopes.
- [x] Make context query-mode long-term text, entities, relations, and citations use a single selected result flow.
- [x] Add include-section and context limit boundary tests.
- [x] Extract CMake helper functions for repeated target include directories and test setup.
- [x] Use `cmake --install` for Linux packaging.
- [x] Add package smoke test for installed headers, library, binary, examples, and minimal SDK linkage.
- [x] Add direct `MemorySqliteStore` unit tests for events, payloads, long-term memory, search, cursors, obsolete entities, and transactions.
- [x] Make consolidation writer fail fast and rely on storage transactions for all-or-nothing writes.
- [x] Deduplicate same agent/type/name entities by marking older active entities obsolete.
- [x] Preserve conflict metadata on `contradicts` relations.
- [x] Add rollback, deduplication, and conflict metadata tests.
