#include "anthropic_model_client.h"

#include <utility>

#include "model_http_client.h"

namespace agent_memory {

AnthropicModelClient::AnthropicModelClient(AnthropicModelConfig config)
    : config_(std::move(config)), httpClient_()
{
}

AnthropicModelClient::AnthropicModelClient(AnthropicModelConfig config, ModelHttpClient httpClient)
    : config_(std::move(config)), httpClient_(std::move(httpClient))
{
}

std::string AnthropicModelClient::Endpoint() const
{
    std::string url = config_.baseUrl;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    const std::string messagesSuffix = "/v1/messages";
    if (url.size() >= messagesSuffix.size() && url.substr(url.size() - messagesSuffix.size()) == messagesSuffix) {
        return url;
    }
    const std::string versionSuffix = "/v1";
    if (url.size() >= versionSuffix.size() && url.substr(url.size() - versionSuffix.size()) == versionSuffix) {
        return url + "/messages";
    }
    return url + messagesSuffix;
}

std::string AnthropicModelClient::BuildRequestBody(const std::string& prompt) const
{
    nlohmann::json body = config_.extraParams.is_object() ? config_.extraParams : nlohmann::json::object();
    body["model"] = config_.modelName;
    body["max_tokens"] = config_.maxTokens > 0 ? config_.maxTokens : 4096;
    body["temperature"] = config_.temperature;
    body["system"] = "You are a memory extraction assistant. Output ONLY valid JSON.";
    body["messages"] = nlohmann::json::array({
        {{"role", "user"}, {"content", prompt}}
    });
    return body.dump();
}

std::string AnthropicModelClient::ParseResponseBody(const std::string& body)
{
    auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_object()) {
        return "";
    }
    if (json.contains("error")) {
        return "";
    }
    if (!json.contains("content") || !json["content"].is_array()) {
        return "";
    }

    std::string text;
    for (const auto& block : json["content"]) {
        if (!block.is_object()) {
            continue;
        }
        if (block.value("type", std::string()) == "text" && block.contains("text") && block["text"].is_string()) {
            text += block["text"].get<std::string>();
        }
    }
    return text;
}

ModelInvokeResult AnthropicModelClient::GenerateMemoryUpdate(const std::string& prompt)
{
    ModelInvokeResult result;
    if (config_.baseUrl.empty() || config_.modelName.empty()) {
        result.errorCode = "invalid_config";
        result.errorMessage = "Anthropic model config requires baseUrl and modelName";
        return result;
    }

    auto headers = config_.headers;
    if (!config_.apiKey.empty()) {
        headers["x-api-key"] = config_.apiKey;
    }
    if (!config_.anthropicVersion.empty()) {
        headers["anthropic-version"] = config_.anthropicVersion;
    }

    auto response = httpClient_.PostJson({Endpoint(), BuildRequestBody(prompt), config_.timeoutSeconds, headers});
    result.httpStatus = response.status;
    if (response.status < 200 || response.status >= 300) {
        result.errorCode = "http_error";
        result.errorMessage = "Anthropic model request failed";
        result.providerError = response.body;
        return result;
    }
    result.text = ParseResponseBody(response.body);
    if (result.text.empty()) {
        result.errorCode = "parse_error";
        result.errorMessage = "Anthropic model response did not contain memory update text";
        result.providerError = response.body;
    }
    return result;
}

} // namespace agent_memory
