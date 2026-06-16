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

可行方案应改为二选一：

1. 保持 API 形状，先接受 `config.h` 继续传播 JSON 依赖，仅把该问题降级为后续 API 清理项。
2. 真正移除公共 JSON 依赖：修改 `MemoryModelConfig::extraParams` 的公共类型，例如改为 JSON 字符串、字符串 map、或自定义轻量 variant；这属于公共 API 变更，建议作为单独设计项处理。

本轮结论：维持现状，暂不修改代码。原因是当前项目阶段更重视 API 稳定性；移除 `config.h` 的 nlohmann 依赖需要公共 API 设计，不能用"仅拆文件"这种低成本改动真正解决。该问题降级为 P2/API 清理项，待 SDK v2.0 或用户明确反馈编译依赖/编译时间问题时再统一处理。

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
- 将 #4 从高优先级问题降级为 P3/防御性增强。
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

### 5. 关于 #17：`float`/`double` 统一为 `double` 的收益有限

认同当前存在类型不一致，但不建议优先把公共 `confidence` 字段从 `float` 改为 `double`。

理由：

- `confidence` 语义范围是 `[0, 1]`，当前精度足够。
- 改成 `double` 会影响公共 SDK ABI/API。
- SQLite 使用 `REAL` 读取为 `double` 是 SQLite C API 的自然行为，不代表领域模型必须使用 `double`。

建议只作为 P3 代码一致性问题处理：保留 `float`，在转换点或字段文档中说明精度取舍即可。

## 其余问题

除以上澄清外，其余问题基本认同，后续可按优先级逐项讨论和修改。
