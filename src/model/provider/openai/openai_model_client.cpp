#include "openai_model_client.h"

#include <utility>

#include "model_http_client.h"

namespace agent_memory {

OpenAiModelClient::OpenAiModelClient(OpenAiModelConfig config)
    : config_(std::move(config))
{
}

std::string OpenAiModelClient::Endpoint() const
{
    std::string url = config_.baseUrl;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    const std::string suffix = "/chat/completions";
    if (url.size() >= suffix.size() && url.substr(url.size() - suffix.size()) == suffix) {
        return url;
    }
    return url + suffix;
}

std::string OpenAiModelClient::BuildRequestBody(const std::string& prompt) const
{
    nlohmann::json body = config_.extraParams.is_object() ? config_.extraParams : nlohmann::json::object();
    body["model"] = config_.modelName;
    body["temperature"] = config_.temperature;
    if (config_.maxTokens > 0) {
        body["max_tokens"] = config_.maxTokens;
    }
    body["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", "You are a memory extraction assistant. Output ONLY valid JSON."}},
        {{"role", "user"}, {"content", prompt}}
    });
    return body.dump();
}

std::string OpenAiModelClient::ParseResponseBody(const std::string& body)
{
    auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_object()) {
        return "";
    }
    if (json.contains("error")) {
        return "";
    }
    if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty()) {
        return "";
    }

    const auto& choice = json["choices"][0];
    if (choice.contains("message") && choice["message"].is_object()) {
        return choice["message"].value("content", std::string());
    }
    if (choice.contains("text") && choice["text"].is_string()) {
        return choice["text"].get<std::string>();
    }
    return "";
}

ModelInvokeResult OpenAiModelClient::GenerateMemoryUpdate(const std::string& prompt)
{
    ModelInvokeResult result;
    if (config_.baseUrl.empty() || config_.modelName.empty()) {
        result.errorCode = "invalid_config";
        result.errorMessage = "OpenAI model config requires baseUrl and modelName";
        return result;
    }

    auto headers = config_.headers;
    if (!config_.apiKey.empty()) {
        headers["Authorization"] = "Bearer " + config_.apiKey;
    }
    if (!config_.organization.empty()) {
        headers["OpenAI-Organization"] = config_.organization;
    }

    auto response = PostJson({Endpoint(), BuildRequestBody(prompt), config_.timeoutSeconds, headers});
    result.httpStatus = response.status;
    if (response.status < 200 || response.status >= 300) {
        result.errorCode = "http_error";
        result.errorMessage = "OpenAI model request failed";
        result.providerError = response.body;
        return result;
    }
    result.text = ParseResponseBody(response.body);
    if (result.text.empty()) {
        result.errorCode = "parse_error";
        result.errorMessage = "OpenAI model response did not contain memory update text";
        result.providerError = response.body;
    }
    return result;
}

} // namespace agent_memory
