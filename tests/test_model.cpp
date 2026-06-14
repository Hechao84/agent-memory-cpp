#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "model_http_client.h"
#include "model_client_factory.h"
#include "provider/anthropic/anthropic_model_client.h"
#include "provider/openai/openai_model_client.h"
#include <nlohmann/json.hpp>

using namespace agent_memory;

namespace {

std::filesystem::path TempFile(const std::string& name)
{
    return std::filesystem::temp_directory_path() / name;
}

bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << text;
    return true;
}

bool ExpectFactoryError(const std::string& name, const std::string& json)
{
    auto path = TempFile(name);
    if (!WriteText(path, json)) {
        return false;
    }
    auto result = LoadModelClientWithResult(path.string());
    if (result || result.error.empty()) {
        std::cerr << "factory error expected for " << name << "\n";
        return false;
    }
    return true;
}

bool TestEndpoint()
{
    OpenAiModelConfig config;
    config.baseUrl = "https://example.com/v1/";
    config.modelName = "test-model";
    OpenAiModelClient client(config);
    if (client.Endpoint() != "https://example.com/v1/chat/completions") {
        std::cerr << "endpoint append failed\n";
        return false;
    }

    config.baseUrl = "https://example.com/v1/chat/completions";
    OpenAiModelClient fullEndpointClient(config);
    if (fullEndpointClient.Endpoint() != "https://example.com/v1/chat/completions") {
        std::cerr << "endpoint passthrough failed\n";
        return false;
    }
    return true;
}

bool TestBuildRequestBody()
{
    OpenAiModelConfig config;
    config.baseUrl = "https://example.com/v1";
    config.modelName = "test-model";
    config.temperature = 0.25;
    config.maxTokens = 128;
    config.extraParams["top_p"] = 0.9;
    OpenAiModelClient client(config);

    auto json = nlohmann::json::parse(client.BuildRequestBody("extract memory"));
    if (json.value("model", "") != "test-model" || json.value("max_tokens", 0) != 128 ||
        json.value("top_p", 0.0) != 0.9 || json["messages"].size() != 2) {
        std::cerr << "request body build failed\n";
        return false;
    }
    return true;
}

bool TestParseResponseBody()
{
    std::string response = R"({"choices":[{"message":{"content":"{\"ok\":true}"}}]})";
    if (OpenAiModelClient::ParseResponseBody(response) != "{\"ok\":true}") {
        std::cerr << "message response parse failed\n";
        return false;
    }

    std::string textResponse = R"({"choices":[{"text":"plain text"}]})";
    if (OpenAiModelClient::ParseResponseBody(textResponse) != "plain text") {
        std::cerr << "text response parse failed\n";
        return false;
    }

    if (!OpenAiModelClient::ParseResponseBody(R"({"error":{"message":"bad"}})").empty()) {
        std::cerr << "error response parse should be empty\n";
        return false;
    }
    return true;
}

bool TestAnthropicEndpoint()
{
    AnthropicModelConfig config;
    config.baseUrl = "https://api.anthropic.com";
    config.modelName = "claude-test";
    AnthropicModelClient client(config);
    if (client.Endpoint() != "https://api.anthropic.com/v1/messages") {
        std::cerr << "anthropic endpoint append failed\n";
        return false;
    }

    config.baseUrl = "https://api.anthropic.com/v1";
    AnthropicModelClient v1EndpointClient(config);
    if (v1EndpointClient.Endpoint() != "https://api.anthropic.com/v1/messages") {
        std::cerr << "anthropic v1 endpoint failed\n";
        return false;
    }

    config.baseUrl = "https://api.anthropic.com/v1/messages";
    AnthropicModelClient fullEndpointClient(config);
    if (fullEndpointClient.Endpoint() != "https://api.anthropic.com/v1/messages") {
        std::cerr << "anthropic endpoint passthrough failed\n";
        return false;
    }
    return true;
}

bool TestAnthropicBuildRequestBody()
{
    AnthropicModelConfig config;
    config.baseUrl = "https://api.anthropic.com";
    config.modelName = "claude-test";
    config.temperature = 0.2;
    config.maxTokens = 256;
    config.extraParams["top_p"] = 0.7;
    AnthropicModelClient client(config);

    auto json = nlohmann::json::parse(client.BuildRequestBody("extract memory"));
    if (json.value("model", "") != "claude-test" || json.value("max_tokens", 0) != 256 ||
        json.value("top_p", 0.0) != 0.7 || json["messages"].size() != 1 || !json.contains("system")) {
        std::cerr << "anthropic request body build failed\n";
        return false;
    }
    return true;
}

bool TestAnthropicParseResponseBody()
{
    std::string response = R"({"content":[{"type":"text","text":"hello"},{"type":"text","text":" world"}]})";
    if (AnthropicModelClient::ParseResponseBody(response) != "hello world") {
        std::cerr << "anthropic response parse failed\n";
        return false;
    }

    if (!AnthropicModelClient::ParseResponseBody(R"({"error":{"message":"bad"}})").empty()) {
        std::cerr << "anthropic error response should be empty\n";
        return false;
    }
    return true;
}

bool TestInvokeUsesHttpTransport()
{
    JsonPostRequest captured;
    SetJsonPostTransportForTesting([&captured](const JsonPostRequest& request) {
        captured = request;
        return HttpResponse{200, R"({"choices":[{"message":{"content":"ok"}}]})"};
    });

    OpenAiModelConfig config;
    config.baseUrl = "https://example.com/v1";
    config.modelName = "test-model";
    config.apiKey = "real-key";
    config.organization = "org-1";
    config.headers["Authorization"] = "Bearer wrong";
    OpenAiModelClient client(config);
    auto openAiResult = client.GenerateMemoryUpdate("prompt");
    if (!openAiResult || openAiResult.text != "ok") {
        ResetJsonPostTransportForTesting();
        std::cerr << "openai invoke transport failed\n";
        return false;
    }
    ResetJsonPostTransportForTesting();
    if (captured.headers["Authorization"] != "Bearer real-key" || captured.headers["OpenAI-Organization"] != "org-1") {
        std::cerr << "openai required headers not applied\n";
        return false;
    }

    SetJsonPostTransportForTesting([](const JsonPostRequest&) {
        return HttpResponse{500, R"({"error":"bad"})"};
    });
    auto httpError = client.GenerateMemoryUpdate("prompt");
    if (httpError || httpError.httpStatus != 500 || httpError.errorCode != "http_error" || httpError.providerError.empty()) {
        ResetJsonPostTransportForTesting();
        std::cerr << "openai http error result failed\n";
        return false;
    }

    SetJsonPostTransportForTesting([](const JsonPostRequest&) {
        return HttpResponse{200, R"({"choices":[]})"};
    });
    auto parseError = client.GenerateMemoryUpdate("prompt");
    if (parseError || parseError.httpStatus != 200 || parseError.errorCode != "parse_error") {
        ResetJsonPostTransportForTesting();
        std::cerr << "openai parse error result failed\n";
        return false;
    }
    ResetJsonPostTransportForTesting();

    SetJsonPostTransportForTesting([&captured](const JsonPostRequest& request) {
        captured = request;
        return HttpResponse{200, R"({"content":[{"type":"text","text":"ok"}]})"};
    });

    AnthropicModelConfig anthropicConfig;
    anthropicConfig.baseUrl = "https://api.anthropic.com";
    anthropicConfig.modelName = "claude-test";
    anthropicConfig.apiKey = "real-key";
    anthropicConfig.anthropicVersion = "2023-06-01";
    anthropicConfig.headers["x-api-key"] = "wrong";
    anthropicConfig.headers["anthropic-version"] = "wrong";
    AnthropicModelClient anthropic(anthropicConfig);
    auto anthropicResult = anthropic.GenerateMemoryUpdate("prompt");
    if (!anthropicResult || anthropicResult.text != "ok") {
        ResetJsonPostTransportForTesting();
        std::cerr << "anthropic invoke transport failed\n";
        return false;
    }
    ResetJsonPostTransportForTesting();
    if (captured.headers["x-api-key"] != "real-key" || captured.headers["anthropic-version"] != "2023-06-01") {
        std::cerr << "anthropic required headers not applied\n";
        return false;
    }
    return true;
}

bool TestFactory()
{
    auto missing = LoadModelClientWithResult("/tmp/agent-memory-cpp-missing-model-config.json");
    if (missing || missing.error.empty()) {
        std::cerr << "missing config should report error\n";
        return false;
    }

    auto invalidPath = TempFile("agent-memory-cpp-invalid-model-config.json");
    if (!WriteText(invalidPath, "not json")) {
        return false;
    }
    auto invalid = LoadModelClientWithResult(invalidPath.string());
    if (invalid || invalid.error.empty()) {
        std::cerr << "invalid JSON should report error\n";
        return false;
    }

    auto validPath = TempFile("agent-memory-cpp-valid-model-config.json");
    if (!WriteText(validPath, R"({
        "formatType":"openai",
        "baseUrl":"https://example.com/v1",
        "apiKey":"secret",
        "modelName":"test-model",
        "temperature":0.1,
        "maxTokens":64,
        "headers":{"X-Test":"yes"},
        "extraParams":{"top_p":0.8}
    })")) {
        return false;
    }
    auto valid = LoadModelClientWithResult(validPath.string());
    if (!valid || !valid.error.empty()) {
        std::cerr << "valid config failed: " << valid.error << "\n";
        return false;
    }

    auto anthropicPath = TempFile("agent-memory-cpp-anthropic-model-config.json");
    if (!WriteText(anthropicPath, R"({
        "formatType":"anthropic",
        "baseUrl":"https://api.anthropic.com",
        "apiKey":"secret",
        "modelName":"claude-test",
        "anthropicVersion":"2023-06-01",
        "maxTokens":256
    })")) {
        return false;
    }
    auto anthropic = LoadModelClientWithResult(anthropicPath.string());
    if (!anthropic || !anthropic.error.empty()) {
        std::cerr << "anthropic config failed: " << anthropic.error << "\n";
        return false;
    }

    auto snakeCasePath = TempFile("agent-memory-cpp-snake-model-config.json");
    if (!WriteText(snakeCasePath, R"({
        "formatType":"anthropic",
        "baseUrl":"https://api.anthropic.com",
        "modelName":"claude-test",
        "anthropic-version":"2023-06-01",
        "max_tokens":256
    })")) {
        return false;
    }
    auto snakeCase = LoadModelClientWithResult(snakeCasePath.string());
    if (!snakeCase || !snakeCase.error.empty()) {
        std::cerr << "snake_case config failed: " << snakeCase.error << "\n";
        return false;
    }

    if (!ExpectFactoryError("agent-memory-cpp-unsupported-model-config.json", R"({
        "formatType":"other",
        "baseUrl":"https://example.com",
        "modelName":"test-model"
    })")) {
        return false;
    }
    if (!ExpectFactoryError("agent-memory-cpp-missing-base-url-model-config.json", R"({
        "formatType":"openai",
        "modelName":"test-model"
    })")) {
        return false;
    }
    if (!ExpectFactoryError("agent-memory-cpp-missing-model-name-config.json", R"({
        "formatType":"openai",
        "baseUrl":"https://example.com"
    })")) {
        return false;
    }
    if (!ExpectFactoryError("agent-memory-cpp-invalid-headers-config.json", R"({
        "formatType":"openai",
        "baseUrl":"https://example.com",
        "modelName":"test-model",
        "headers":["bad"]
    })")) {
        return false;
    }
    if (!ExpectFactoryError("agent-memory-cpp-invalid-extra-params-config.json", R"({
        "formatType":"openai",
        "baseUrl":"https://example.com",
        "modelName":"test-model",
        "extraParams":["bad"]
    })")) {
        return false;
    }
    if (!ExpectFactoryError("agent-memory-cpp-invalid-timeout-config.json", R"({
        "formatType":"openai",
        "baseUrl":"https://example.com",
        "modelName":"test-model",
        "timeoutSeconds":0
    })")) {
        return false;
    }
    if (!ExpectFactoryError("agent-memory-cpp-invalid-max-tokens-config.json", R"({
        "formatType":"openai",
        "baseUrl":"https://example.com",
        "modelName":"test-model",
        "maxTokens":-1
    })")) {
        return false;
    }
    if (!ExpectFactoryError("agent-memory-cpp-invalid-temperature-config.json", R"({
        "formatType":"openai",
        "baseUrl":"https://example.com",
        "modelName":"test-model",
        "temperature":3.0
    })")) {
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!TestEndpoint()) {
        return 1;
    }
    if (!TestBuildRequestBody()) {
        return 1;
    }
    if (!TestParseResponseBody()) {
        return 1;
    }
    if (!TestAnthropicEndpoint()) {
        return 1;
    }
    if (!TestAnthropicBuildRequestBody()) {
        return 1;
    }
    if (!TestAnthropicParseResponseBody()) {
        return 1;
    }
    if (!TestInvokeUsesHttpTransport()) {
        return 1;
    }
    if (!TestFactory()) {
        return 1;
    }
    return 0;
}
