#include "openai_memory_model_client.h"

#include <curl/curl.h>

#include <utility>

#include <nlohmann/json.hpp>

namespace agent_memory {

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), total);
    return total;
}

} // namespace

OpenAiMemoryModelClient::OpenAiMemoryModelClient(OpenAiMemoryModelConfig config)
    : config_(std::move(config))
{
}

std::string OpenAiMemoryModelClient::Endpoint() const
{
    std::string url = config_.baseUrl;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    if (url.size() >= 16 && url.substr(url.size() - 16) == "/chat/completions") {
        return url;
    }
    return url + "/chat/completions";
}

OpenAiMemoryModelClient::HttpResponse OpenAiMemoryModelClient::PostJson(const std::string& body) const
{
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return response;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config_.apiKey.empty()) {
        std::string auth = "Authorization: Bearer " + config_.apiKey;
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, Endpoint().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.timeoutSeconds));

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }

    if (headers != nullptr) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    return response;
}

std::string OpenAiMemoryModelClient::InvokeMemoryExtraction(const std::string& prompt)
{
    if (config_.baseUrl.empty() || config_.modelName.empty()) {
        return "";
    }

    nlohmann::json body;
    body["model"] = config_.modelName;
    body["temperature"] = 0;
    body["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", "You are a memory extraction assistant. Output ONLY valid JSON."}},
        {{"role", "user"}, {"content", prompt}}
    });

    auto response = PostJson(body.dump());
    if (response.status < 200 || response.status >= 300) {
        return "";
    }

    auto json = nlohmann::json::parse(response.body, nullptr, false);
    if (!json.is_object() || !json.contains("choices") || !json["choices"].is_array() || json["choices"].empty()) {
        return "";
    }

    const auto& choice = json["choices"][0];
    if (choice.contains("message") && choice["message"].is_object()) {
        return choice["message"].value("content", std::string());
    }
    return "";
}

} // namespace agent_memory
