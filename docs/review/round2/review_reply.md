# 第二轮架构审视回复

## 总体态度

整体认同本轮 review 的问题识别。多数问题确实存在，且修复方向合理。以下仅记录不完全认同或需要澄清的点。

## 不完全认同点

### 1. 关于 #5：`config.h` 拆出 `MemoryModelConfig` 的最低行动不充分

认同"公共 SDK 头文件传播 `nlohmann/json.hpp` 依赖"是问题，但不完全认同报告中给出的 P1 最低行动：

> 将 `MemoryModelConfig` 从 `config.h` 拆到 `model_config.h`，`config.h` 不再 `#include <nlohmann/json.hpp>`。

当前 `MemoryConfig` 在 `config.h` 中以值成员形式包含：

```cpp
MemoryModelConfig model;
```

因此只把 `MemoryModelConfig` 移到 `model_config.h` 并不能让 `config.h` 摆脱 `nlohmann/json.hpp`：`config.h` 仍必须包含 `model_config.h` 才能获得完整类型定义。

可行方案应改为：

1. 保持 API 形状，先接受 `config.h` 继续传播 JSON 依赖，仅把该问题降级为后续 API 清理项。
2. 真正移除公共 JSON 依赖：修改 `MemoryModelConfig::extraParams` 的公共类型，例如改为 JSON 字符串、字符串 map、或自定义轻量 variant；这属于公共 API 变更，建议作为单独设计项处理。
3. 或者改变 `MemoryConfig::model` 的拥有方式，例如改成 pimpl/可前向声明的拥有类型，但这同样是公共 API/ABI 设计问题。

本轮结论：维持现状，暂不修改代码。原因是当前项目阶段更重视 API 稳定性；这是编译依赖/API 清理问题，不是运行时正确性、安全问题或数据丢失问题。移除 `config.h` 的 nlohmann 依赖需要公共 API 设计，不能用"仅拆文件"这种低成本改动真正解决。该问题降级为 P2/API 清理项，待 SDK v2.0 或用户明确反馈 nlohmann 可用性/编译时间成本问题时再统一处理。`todo.md` 已同步将该项从 P1 调整为 P2，并移除"仅拆文件即可解决"的错误方案。

### 2. 关于 #4：非法 cursor 的严重性略高估，当前可暂不修改

认同 `CursorToInt` 静默把非法 cursor 转为 0 不是最理想的防御性实现，但基于当前代码路径，暂不认为它需要立即修复。

当前 `LoadEventsAfterCursor` 属于内部 Store 接口，主要由 Runtime 的 consolidation 流程调用。cursor 通常来自 Store 自己生成并保存的 `memory_consolidation_cursors`，不是 SDK/REST/MCP 直接暴露的用户输入。因此只要内部 cursor 生成与保存路径保持正确，正常运行中不会出现非法 cursor。

当前真实风险主要来自：

- DB 被手工篡改；
- DB 损坏；
- 未来版本 bug 写入了非法 cursor；
- 未来如果把 cursor 作为外部输入暴露。

这些场景下，非法 cursor 回退为 0 会导致重复读取历史事件，进而可能造成重复 consolidation、额外模型调用成本，或长期记忆重复/污染。但这不是当前主路径风险，也不是外部输入攻击面。

结论：

- 当前阶段可暂不修改 #4。
- 将 #4 从高优先级问题明确降级为 P3/防御性增强，而不是 P2。
- 降级原因：cursor 当前不是 SDK/REST/MCP 的外部输入，主要由 Store 自己生成并保存；风险只在 DB 被手工篡改、DB 损坏、未来版本 bug 写入非法 cursor，或未来把 cursor 暴露为外部输入时出现。
- 当前影响主要是重复读取历史事件、重复 consolidation、额外模型调用成本或长期记忆重复/污染；它不是当前主路径的安全风险，也不是数据丢失风险。
- 后续建议结合 #6/#7 的 Store 错误传播重构一起处理：让 Store 读方法能返回结构化错误；届时非法 cursor 应返回明确错误，而不是静默回退为 0。

### 3. 关于 #19：测试覆盖不足的处理结论

认同 #19 指出的测试覆盖不足问题。当前已补齐 P1 主体覆盖：

- `file_util`、`path_util`、`payload_query`、`runtime_paths`、`runtime_store_initializer` 的关键单元测试；
- payload 越界读取、缺失文件、空 search query 等负向场景；
- REST `/v1/events`、`/v1/context`、`/v1/payloads`、`/v1/search`、`/v1/stats`、`/v1/consolidate` 的基本成功路径；
- HTTP auth 缺失、malformed JSON；
- MCP `initialize`、`tools/list`、7 个 tool call、unknown method、unknown tool、parse error、notification。

因此 #19 的 P1 风险已基本闭环。

剩余建议作为 P2 扩展覆盖处理，不阻塞本轮架构修复：

1. 先定义 `AppendEvent` 空 `agentId/sessionId` 的产品语义。建议拒绝并返回 validation error；这是行为设计，不应只补测试。
2. 为 `ContextBuilder` 增加独立单元测试，覆盖 section 组合、Store 读失败传播、query 过滤；当前主要通过 runtime 间接覆盖。
3. 为 `PayloadService` 增加更完整的独立单元测试，覆盖 atomic write、read error、DB 失败清理、GC；当前已有 runtime 间接覆盖和失败 Store 覆盖。
4. 继续补 HTTP/MCP 边界组合：wrong auth token、unknown route、invalid params、MCP notification 经 HTTP 返回 202 等。
5. `curl_client` 直接测试暂缓，建议等模型客户端/HTTP transport 重构时配合 mock HTTP 或本地测试 server 一起补。

### 4. 关于 #8：配置解析 helper 重复的处理边界

认同 `model_config.cpp`、`model_client_factory.cpp`、`server_options.cpp` 中存在配置解析 helper 重复，但不建议把 server 侧解析逻辑改成宽松模式，也不建议为了合并而把 server 专属的严格 helper 抽到通用层。

原因：

- model 侧解析是宽松语义：字段缺失或类型不匹配时使用默认值，并由后续 model validate 给出错误。
- server 侧解析是严格语义：部署配置类型写错应尽早失败，不能静默回退默认值；否则可能导致端口、auth、payload limit、MCP path 等关键配置与用户预期不一致。
- 如果严格 helper 目前只有 `server_options.cpp` 使用，保留在该文件匿名命名空间中更清晰，抽到 `json_helpers.h` 收益有限，还会让通用 JSON helper 承担 server 专属策略。

本轮处理边界：

- 只抽取 model 侧共用宽松 helper：`JsonDouble`、`JsonStringMap`、`HasInvalidString`、`HasInvalidInteger`、`HasInvalidNumber`、`HasInvalidObject`。
- `model_config.cpp` 和 `model_client_factory.cpp` 删除重复的 `LoadString`/`LoadInt`/`LoadDouble`/`LoadHeaders`/`HasInvalid*`，改用 `json_helpers.h`。
- `server_options.cpp` 保持现状，继续保留严格 `LoadString`/`LoadInt`/`LoadSize`/`LoadBool`/`LoadSubObject`。

### 5. 关于 #9：Anthropic/OpenAI 模型客户端逻辑重复的处理结论

认同两个 provider 的 `GenerateMemoryUpdate` 存在公共流程重复，但不建议当前立即抽象。

理由：

- 目前业界和本项目主要适配 OpenAI-compatible 与 Anthropic-compatible 两类接口，新增第三类 provider 的短期概率不高。
- 两类接口的差异点较多，包括认证 header、版本 header、endpoint 后缀规则、system prompt 放置位置、请求体字段、响应体结构和默认 `max_tokens` 语义。分开实现更直观，也更便于按 provider 行为独立调整。
- 当前重复主要集中在少量流程代码：配置校验、HTTP POST、状态码处理、空响应文本处理。重复规模有限，尚未造成维护风险。

结论：暂时不改，降级为 P3/条件触发重构项。后续如果新增第三类 provider，或模型层整体重构，可考虑抽内部基类/模板方法，只复用公共 `GenerateMemoryUpdate` 流程；子类继续实现 `ProviderName`、`Endpoint`、`Headers`、`BuildRequestBody`、`ParseResponseBody` 等差异点。

### 6. 关于 #10：搜索权重和默认限制值硬编码的处理结论

认同这些默认值目前散布在不同模块中，但暂不计划通过公共 API 暴露给业务侧。

涉及的默认值包括：

- 搜索评分权重：summary/entity/relation 权重，以及 LIKE fallback 分数。
- Context 构建限制：`message_limit`、`payload_limit`、`long_term_limit` 的默认值或动态推导规则。
- Store 查询限制：recent events、long-term memory、search 的默认 limit。
- Consolidation 写入 confidence：session/topic/profile summary 的默认置信度。

处理结论：当前阶段保留这些默认值。原因是它们属于产品策略和调参项，不是运行时正确性、安全或数据一致性问题；当前还缺少足够真实使用数据支撑配置面设计。后续在具体使用和评估中通过调参优化默认值取值，但暂时不把这些参数暴露到 `MemoryConfig` 或业务侧 API，避免过早扩大公共 API 面。

后续如果默认值继续增多，可先提取为内部 `defaults` 常量集合，提升代码可读性；是否开放配置应等有明确业务调优需求后再评估。

### 7. 关于 #20：LLM prompt 模板硬编码的处理结论

认同当前 consolidation prompt 模板硬编码在 `LlmLongTermMemoryProcessor` 中，修改 prompt 需要重新编译。

处理结论：当前阶段暂时保留硬编码方式，不立即改为配置或文件读取。

理由：

- 当前 prompt/schema 仍处于早期稳定阶段，频繁变化时先保持代码内收敛更容易测试和控制行为。
- 过早开放 prompt 模板会扩大配置面，并引入模板变量、schema 兼容、错误回退、文件路径安全和部署布局等额外设计问题。
- 业务侧如需完全自定义模型调用逻辑，当前已经可以通过 `Consolidate(request, ModelClient*)` 传入宿主 `ModelClient` 实现。

后续触发条件：当内置 prompt/schema 稳定，且出现明确的多业务定制需求时，再将 prompt 模板改为从文件读取或通过配置指定，并补充模板变量和回退策略设计。

### 8. 关于 #12/#13/#14/#21：文档类问题的集中处理结论

认同这些问题本质上属于文档债，且会影响 SDK/Server 使用者理解系统边界。本轮集中更新文档，不改运行时代码：

- #12 文档与代码不一致：补充 `MemoryEvent::storeCursor`、`MemoryPayloadReadResult`、`MemoryModelStatus`、错误码目录、consolidation 无事件语义、`forceReprocess=true` + `maxEvents<=0` 边界、`payload_query`/`server_cli` 模块、Server `model.strict` 与 `MemoryModelConfig` 的映射关系。
- #13 线程安全文档不完整：补充 Runtime、Store、PayloadService、ContextBuilder、HTTP/MCP handler、ModelClient/ModelHttpClient 的并发边界和锁顺序约定。
- #14 公共 SDK 头文件无文档注释：为 `include/agent_memory/` 公共头文件补充结构化注释，明确关键字段语义和 Result/Error 的 bool 语义差异。
- #21 安全相关文档缺失：补充 Transport、Storage、Model 的部署安全说明，包括 TLS/CORS/rate limiting 由反向代理承担、non-loopback host 警告、配置/API key 文件权限、数据库/payload 备份一致性和文件权限现状。

同时将“代码改动涉及接口、行为、配置、错误码、存储、并发、安全或部署语义时，同步更新文档”写入 `AGENTS.md`，作为后续协作约定。

### 9. 关于 #11：`enablePayloadOffload` SDK/Server 默认值差异的处理结论

初始 review 指出 SDK 默认 `false`、Server 默认 `true` 且文档未说明。后续复核认为当前没有旧用户兼容包袱，保持差异反而会增加理解成本。

处理结论：将 SDK `MemoryConfig::enablePayloadOffload` 默认值改为 `true`，与 Server `memory.enablePayloadOffload` 默认一致。

语义说明：`enablePayloadOffload=true` 只表示允许 offload；实际只有当 `content.size() >= offloadThresholdChars` 时才会写入 payload 文件。默认阈值仍为 `8000`，低于阈值的内容继续直接返回原文。SDK 调用方如果不希望产生 payload 文件副作用，可以显式设置 `enablePayloadOffload=false`。

### 10. 关于 #17：`float`/`double` 统一为 `double` 的收益有限

认同当前存在类型不一致，但不建议优先把公共 `confidence` 字段从 `float` 改为 `double`。

理由：

- `confidence` 语义范围是 `[0, 1]`，当前精度足够。
- 改成 `double` 会影响公共 SDK ABI/API。
- SQLite 使用 `REAL` 读取为 `double` 是 SQLite C API 的自然行为，不代表领域模型必须使用 `double`。

建议只作为 P3 代码一致性问题处理：保留 `float`，在转换点或字段文档中说明精度取舍即可。

## 其余问题

除以上澄清外，其余问题基本认同，后续可按优先级逐项讨论和修改。
