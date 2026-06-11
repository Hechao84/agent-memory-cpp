#include "agent_memory/http_memory_runtime.h"

#include <curl/curl.h>

#include <sstream>
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

nlohmann::json EventToJson(const MemoryEvent& event)
{
    nlohmann::json j;
    j["type"] = static_cast<int>(event.type);
    j["agentId"] = event.agentId;
    j["sessionId"] = event.sessionId;
    j["role"] = event.role;
    j["content"] = event.content;
    j["toolCallId"] = event.toolCallId;
    j["toolName"] = event.toolName;
    j["payloadRef"] = event.payloadRef;
    return j;
}

nlohmann::json ContextRequestToJson(const MemoryContextRequest& request)
{
    nlohmann::json j;
    j["agentId"] = request.agentId;
    j["sessionId"] = request.sessionId;
    j["query"] = request.query;
    j["tokenBudget"] = request.tokenBudget;
    return j;
}

nlohmann::json ConsolidationRequestToJson(const MemoryConsolidationRequest& request)
{
    nlohmann::json j;
    j["agentId"] = request.agentId;
    j["sessionId"] = request.sessionId;
    j["maxEvents"] = request.maxEvents;
    j["force"] = request.force;
    return j;
}

nlohmann::json SearchRequestToJson(const MemorySearchRequest& request)
{
    nlohmann::json j;
    j["agentId"] = request.agentId;
    j["sessionId"] = request.sessionId;
    j["query"] = request.query;
    j["limit"] = request.limit;
    return j;
}

} // namespace

HttpMemoryRuntime::HttpMemoryRuntime(MemoryConfig config)
    : MemoryRuntime(std::move(config)), serverUrl_(config_.serverUrl)
{
    while (!serverUrl_.empty() && serverUrl_.back() == '/') {
        serverUrl_.pop_back();
    }
}

HttpMemoryRuntime::HttpResponse HttpMemoryRuntime::Get(const std::string& path) const
{
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return response;
    }
    struct curl_slist* headers = nullptr;
    if (!config_.serverApiKey.empty()) {
        std::string auth = "Authorization: Bearer " + config_.serverApiKey;
        headers = curl_slist_append(headers, auth.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, UrlForPath(path).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.serverTimeoutSeconds));
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

HttpMemoryRuntime::HttpResponse HttpMemoryRuntime::Post(const std::string& path, const std::string& body) const
{
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return response;
    }
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config_.serverApiKey.empty()) {
        std::string auth = "Authorization: Bearer " + config_.serverApiKey;
        headers = curl_slist_append(headers, auth.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, UrlForPath(path).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.serverTimeoutSeconds));
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

std::string HttpMemoryRuntime::UrlForPath(const std::string& path) const
{
    if (path.empty() || path.front() != '/') {
        return serverUrl_ + "/" + path;
    }
    return serverUrl_ + path;
}

std::string HttpMemoryRuntime::EncodeRefPath(const std::string& ref) const
{
    std::string path = ref;
    const std::string prefix = "file://";
    if (path.rfind(prefix, 0) == 0) {
        path = path.substr(prefix.size());
    }
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return path;
    }
    char* escaped = curl_easy_escape(curl, path.c_str(), static_cast<int>(path.size()));
    std::string out = escaped != nullptr ? escaped : path;
    if (escaped != nullptr) {
        curl_free(escaped);
    }
    curl_easy_cleanup(curl);
    return out;
}

bool HttpMemoryRuntime::AppendEvent(const MemoryEvent& event)
{
    auto response = Post("/v1/events", EventToJson(event).dump());
    if (response.status < 200 || response.status >= 300) {
        return false;
    }
    auto json = nlohmann::json::parse(response.body, nullptr, false);
    return json.is_object() && json.value("ok", false);
}

MemoryContextPackage HttpMemoryRuntime::BuildContext(const MemoryContextRequest& request)
{
    MemoryContextPackage pkg;
    auto response = Post("/v1/context", ContextRequestToJson(request).dump());
    if (response.status < 200 || response.status >= 300) {
        return pkg;
    }
    auto json = nlohmann::json::parse(response.body, nullptr, false);
    if (!json.is_object()) {
        return pkg;
    }
    pkg.memoryText = json.value("memoryText", std::string());
    if (json.contains("metadata") && json["metadata"].is_object()) {
        for (auto it = json["metadata"].begin(); it != json["metadata"].end(); ++it) {
            if (it.value().is_string()) {
                pkg.metadata[it.key()] = it.value().get<std::string>();
            }
        }
    }
    return pkg;
}

MemoryPayloadWriteResult HttpMemoryRuntime::WritePayload(const MemoryPayloadWriteRequest& request)
{
    MemoryPayloadWriteResult result;
    result.replacementContent = request.content;

    nlohmann::json body;
    body["agentId"] = request.agentId;
    body["sessionId"] = request.sessionId;
    body["content"] = request.content;
    body["contentType"] = request.contentType;
    body["toolCallId"] = request.toolCallId;
    body["toolName"] = request.toolName;

    auto response = Post("/v1/payloads", body.dump());
    if (response.status < 200 || response.status >= 300) {
        return result;
    }
    auto json = nlohmann::json::parse(response.body, nullptr, false);
    if (!json.is_object() || !json.value("ok", false)) {
        return result;
    }
    result.offloaded = json.value("offloaded", false);
    result.replacementContent = json.value("replacementContent", request.content);
    if (json.contains("payload") && json["payload"].is_object()) {
        const auto& payload = json["payload"];
        result.payload.ref = payload.value("ref", std::string());
        result.payload.contentType = payload.value("contentType", std::string());
        result.payload.summary = payload.value("summary", std::string());
        result.payload.toolName = payload.value("toolName", std::string());
        result.payload.originalChars = payload.value("originalChars", 0);
    }
    return result;
}

std::string HttpMemoryRuntime::ReadPayload(const std::string& ref)
{
    auto response = Get("/v1/payloads/" + EncodeRefPath(ref));
    if (response.status < 200 || response.status >= 300) {
        return "";
    }
    auto json = nlohmann::json::parse(response.body, nullptr, false);
    if (!json.is_object() || !json.value("ok", false)) {
        return "";
    }
    return json.value("content", std::string());
}

bool HttpMemoryRuntime::Consolidate(const MemoryConsolidationRequest& request)
{
    auto response = Post("/v1/consolidate", ConsolidationRequestToJson(request).dump());
    if (response.status < 200 || response.status >= 300) {
        return false;
    }
    auto json = nlohmann::json::parse(response.body, nullptr, false);
    return json.is_object() && json.value("handled", false);
}

std::vector<MemorySearchResult> HttpMemoryRuntime::SearchMemory(const MemorySearchRequest& request)
{
    std::vector<MemorySearchResult> results;
    auto response = Post("/v1/search", SearchRequestToJson(request).dump());
    if (response.status < 200 || response.status >= 300) {
        return results;
    }
    auto json = nlohmann::json::parse(response.body, nullptr, false);
    if (!json.is_object() || !json.contains("results") || !json["results"].is_array()) {
        return results;
    }
    for (const auto& item : json["results"]) {
        if (!item.is_object()) {
            continue;
        }
        MemorySearchResult result;
        result.id = item.value("id", std::string());
        result.type = item.value("type", std::string());
        result.content = item.value("content", std::string());
        result.score = item.value("score", 0.0F);
        results.push_back(std::move(result));
    }
    return results;
}

MemoryStats HttpMemoryRuntime::GetStats() const
{
    MemoryStats stats;
    auto response = Get("/v1/stats");
    if (response.status < 200 || response.status >= 300) {
        return stats;
    }
    auto json = nlohmann::json::parse(response.body, nullptr, false);
    if (!json.is_object()) {
        return stats;
    }
    stats.events = json.value("events", 0);
    stats.payloads = json.value("payloads", 0);
    stats.summaries = json.value("summaries", 0);
    stats.entities = json.value("entities", 0);
    stats.relations = json.value("relations", 0);
    return stats;
}

} // namespace agent_memory
