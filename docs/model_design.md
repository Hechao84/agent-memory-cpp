# Model 模块设计

## 模块目标

Model 模块为长期记忆抽取提供可插拔模型能力。SDK 和 Server 都可通过 `MemoryConfig.model` 加载 OpenAI-compatible 或 Anthropic-compatible 内置客户端；SDK 也可实现 `ModelClient` 并在单次 `Consolidate` 调用中显式传入宿主模型。

## 主要文件

- `include/agent_memory/model_client.h`
- `src/model/model_config.*`
- `src/model/model_client_factory.*`
- `src/model/model_http_client.*`
- `src/model/provider/openai/openai_model_client.*`
- `src/model/provider/anthropic/anthropic_model_client.*`

## 对外接口

```cpp
class ModelClient
{
public:
    virtual ~ModelClient() = default;
    virtual ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) = 0;
};
```

`ModelInvokeResult` 字段：

| 字段 | 说明 |
| --- | --- |
| `text` | 模型返回的长期记忆 JSON 文本 |
| `httpStatus` | HTTP 状态码，宿主自定义模型可置 0 |
| `errorCode` | 错误码 |
| `errorMessage` | 错误说明 |
| `providerError` | 上游模型原始错误内容 |

## 配置结构

SDK 使用 `MemoryConfig.model` 配置内置模型；Server 模式的顶层 `model` 配置会在启动时映射到同一结构：

| 字段 | 说明 |
| --- | --- |
| `enabled` | 是否启用模型 |
| `strict` | 模型配置失败时是否直接启动失败 |
| `formatType` | `openai` 或 `anthropic` |
| `baseUrl` | 模型服务地址 |
| `apiKey` | API Key |
| `modelName` | 模型名称 |
| `timeoutSeconds` | 请求超时 |
| `temperature` | 采样温度，范围 0 到 2 |
| `maxTokens` / `max_tokens` | 最大输出 token |
| `headers` | 额外 HTTP 请求头 |
| `extraParams` | 额外请求体参数 |
| `organization` | OpenAI organization header |
| `anthropicVersion` / `anthropic-version` | Anthropic version header |

## OpenAI-compatible 适配

`OpenAiModelClient` 请求 endpoint：

- 如果 `baseUrl` 已以 `/chat/completions` 结尾，直接使用。
- 否则拼接 `/chat/completions`。

请求体：

- `model`
- `temperature`
- `max_tokens`
- `messages`
  - system：要求只输出合法 JSON。
  - user：长期记忆抽取 prompt。

认证：

- 如果 `apiKey` 非空，设置 `Authorization: Bearer <apiKey>`。
- 如果 `organization` 非空，设置 `OpenAI-Organization`。

响应解析：

- 优先读取 `choices[0].message.content`。
- 兼容读取 `choices[0].text`。

## Anthropic-compatible 适配

`AnthropicModelClient` 请求 endpoint：

- 如果 `baseUrl` 以 `/v1/messages` 结尾，直接使用。
- 如果 `baseUrl` 以 `/v1` 结尾，拼接 `/messages`。
- 否则拼接 `/v1/messages`。

请求体：

- `model`
- `max_tokens`
- `temperature`
- `system`
- `messages`

认证：

- 如果 `apiKey` 非空，设置 `x-api-key`。
- 如果 `anthropicVersion` 非空，设置 `anthropic-version`。

响应解析：

- 遍历 `content` 数组。
- 拼接 `type=text` 块中的 `text` 字段。

## HTTP 调用层

`ModelHttpClient` 是模型 provider 的实例级 HTTP 调用封装。每个 `OpenAiModelClient` / `AnthropicModelClient` 持有自己的 `ModelHttpClient`，默认 transport 使用 libcurl 发送 JSON POST。

测试或内部工厂可以为单个模型实例注入自定义 `JsonPostTransport`，不会影响同进程中的其他模型实例或 runtime。transport 闭包如果共享外部状态，需要由注入方自行保证线程安全。

## 加载逻辑

`LoadModelClientFromConfig` 和 `LoadModelClientFromJson` 按 `formatType` 创建模型客户端：

- `openai` -> `OpenAiModelClient`
- `anthropic` -> `AnthropicModelClient`
- 其他值返回 unsupported 错误

公共校验包括：

- `baseUrl`、`modelName` 必填。
- `timeoutSeconds` 必须为正数。
- `maxTokens` 非负。
- `temperature` 在 0 到 2 之间。
- `headers`、`extraParams` 必须为 object。

## 与其他模块关系

```text
BuiltinMemoryRuntime
  -> RuntimeServices::modelClient
     -> OpenAiModelClient / AnthropicModelClient
ConsolidationService
  -> LlmLongTermMemoryProcessor
     -> ModelClient
        -> Runtime builtin model / Host implementation
```

`Consolidate(request)` 使用 runtime 内置模型；`Consolidate(request, &model)` 使用宿主显式传入模型并覆盖内置模型；`Consolidate(request, nullptr)` 表示本次调用禁用模型。SDK 调用方可通过 `BuiltinMemoryRuntime::GetModelStatus()` 查询内置模型是否已成功初始化。如果模型不可用或返回空结果，Consolidation 模块会使用规则处理器回退；Server 配置 `model.strict=true` 时，模型配置校验失败会导致启动失败。
