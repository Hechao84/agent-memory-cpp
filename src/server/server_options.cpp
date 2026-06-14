#include "server_options.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace agent_memory {

namespace {

bool IsAllowedKey(const std::string& key, const std::vector<std::string>& allowed)
{
    for (const auto& item : allowed) {
        if (key == item) {
            return true;
        }
    }
    return false;
}

void ValidateKeys(const nlohmann::json& j, const std::vector<std::string>& allowed, const std::string& prefix)
{
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!IsAllowedKey(it.key(), allowed)) {
            throw std::runtime_error("unknown server config key: " + prefix + it.key());
        }
    }
}

std::string LoadString(const nlohmann::json& j, const std::string& key, const std::string& defaultValue = "")
{
    if (!j.contains(key)) {
        return defaultValue;
    }
    if (!j[key].is_string()) {
        throw std::runtime_error("server config key must be a string: " + key);
    }
    return j[key].get<std::string>();
}

int LoadInt(const nlohmann::json& j, const std::string& key, int defaultValue)
{
    if (!j.contains(key)) {
        return defaultValue;
    }
    if (!j[key].is_number_integer()) {
        throw std::runtime_error("server config key must be an integer: " + key);
    }
    return j[key].get<int>();
}

std::size_t LoadSize(const nlohmann::json& j, const std::string& key, std::size_t defaultValue)
{
    if (!j.contains(key)) {
        return defaultValue;
    }
    if (!j[key].is_number_unsigned()) {
        throw std::runtime_error("server config key must be an unsigned integer: " + key);
    }
    std::uint64_t value = j[key].get<std::uint64_t>();
    if (value > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
        throw std::runtime_error("server config key is too large: " + key);
    }
    return static_cast<std::size_t>(value);
}

bool LoadBool(const nlohmann::json& j, const std::string& key, bool defaultValue)
{
    if (!j.contains(key)) {
        return defaultValue;
    }
    if (!j[key].is_boolean()) {
        throw std::runtime_error("server config key must be a boolean: " + key);
    }
    return j[key].get<bool>();
}

nlohmann::json LoadSubObject(const nlohmann::json& j, const std::string& key)
{
    if (!j.contains(key)) {
        return nlohmann::json::object();
    }
    if (!j[key].is_object()) {
        throw std::runtime_error("server config key must be an object: " + key);
    }
    return j[key];
}

void LoadMemoryOptions(const nlohmann::json& memory, ServerOptions& opts)
{
    ValidateKeys(memory, {"dataPath", "enablePayloadOffload", "offloadThreshold", "tokenBudget"}, "memory.");
    opts.dataPath = LoadString(memory, "dataPath", opts.dataPath);
    opts.enablePayloadOffload = LoadBool(memory, "enablePayloadOffload", opts.enablePayloadOffload);
    opts.offloadThreshold = LoadInt(memory, "offloadThreshold", opts.offloadThreshold);
    opts.tokenBudget = LoadInt(memory, "tokenBudget", opts.tokenBudget);
}

void LoadModelOptions(const nlohmann::json& model, ServerOptions& opts)
{
    ValidateKeys(model, {"enabled", "strict", "formatType", "provider", "baseUrl", "apiKey", "modelName", "organization", "anthropicVersion", "anthropic-version", "timeoutSeconds", "maxTokens", "max_tokens", "temperature", "headers", "extraParams"}, "model.");
    opts.strictModelConfig = LoadBool(model, "strict", opts.strictModelConfig);
    if (LoadBool(model, "enabled", true)) {
        opts.modelConfig = model;
    }
}

void LoadHttpOptions(const nlohmann::json& http, ServerOptions& opts)
{
    ValidateKeys(http, {"host", "port", "maxPayloadBytes", "readTimeoutSeconds", "writeTimeoutSeconds", "threadCount"}, "server.http.");
    opts.http.host = LoadString(http, "host", opts.http.host);
    opts.http.port = LoadInt(http, "port", opts.http.port);
    opts.http.maxPayloadBytes = LoadSize(http, "maxPayloadBytes", opts.http.maxPayloadBytes);
    opts.http.readTimeoutSeconds = LoadInt(http, "readTimeoutSeconds", opts.http.readTimeoutSeconds);
    opts.http.writeTimeoutSeconds = LoadInt(http, "writeTimeoutSeconds", opts.http.writeTimeoutSeconds);
    opts.http.threadCount = LoadInt(http, "threadCount", opts.http.threadCount);
}

void LoadAuthOptions(const nlohmann::json& auth, ServerOptions& opts)
{
    ValidateKeys(auth, {"apiToken"}, "server.auth.");
    opts.apiToken = LoadString(auth, "apiToken", opts.apiToken);
}

void LoadMcpOptions(const nlohmann::json& mcp, ServerOptions& opts)
{
    ValidateKeys(mcp, {"mode", "path", "maxMessageBytes"}, "server.mcp.");
    opts.mcp.mode = LoadString(mcp, "mode", opts.mcp.mode);
    opts.mcp.path = LoadString(mcp, "path", opts.mcp.path);
    opts.mcp.maxMessageBytes = LoadSize(mcp, "maxMessageBytes", opts.mcp.maxMessageBytes);
}

void LoadServerSection(const nlohmann::json& server, ServerOptions& opts)
{
    ValidateKeys(server, {"debugErrors", "auth", "http", "mcp"}, "server.");
    opts.debugErrors = LoadBool(server, "debugErrors", opts.debugErrors);
    LoadAuthOptions(LoadSubObject(server, "auth"), opts);
    LoadHttpOptions(LoadSubObject(server, "http"), opts);
    LoadMcpOptions(LoadSubObject(server, "mcp"), opts);
}

} // namespace

ServerOptions LoadServerOptions(const nlohmann::json& j)
{
    ValidateKeys(j, {"memory", "model", "server"}, "");

    ServerOptions opts;
    LoadMemoryOptions(LoadSubObject(j, "memory"), opts);
    if (j.contains("model")) {
        LoadModelOptions(LoadSubObject(j, "model"), opts);
    }
    LoadServerSection(LoadSubObject(j, "server"), opts);
    return opts;
}

ServerOptions LoadServerOptionsFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open server config: " + path);
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        throw std::runtime_error("invalid server config JSON: " + path);
    }
    return LoadServerOptions(j);
}

void ValidateServerOptions(const ServerOptions& opts, bool httpMode)
{
    if (opts.dataPath.empty()) {
        throw std::runtime_error("memory.dataPath must not be empty");
    }
    if (opts.offloadThreshold < 0) {
        throw std::runtime_error("memory.offloadThreshold must be non-negative");
    }
    if (opts.tokenBudget <= 0) {
        throw std::runtime_error("memory.tokenBudget must be positive");
    }
    if (opts.mcp.mode != "http") {
        throw std::runtime_error("server.mcp.mode currently supports only http");
    }
    if (opts.mcp.path.empty() || opts.mcp.path[0] != '/') {
        throw std::runtime_error("server.mcp.path must start with /");
    }
    if (opts.mcp.maxMessageBytes == 0) {
        throw std::runtime_error("server.mcp.maxMessageBytes must be positive");
    }
    if (httpMode) {
        if (opts.http.host.empty()) {
            throw std::runtime_error("server.http.host must not be empty");
        }
        if (opts.http.port < 1 || opts.http.port > 65535) {
            throw std::runtime_error("server.http.port must be between 1 and 65535");
        }
        if (opts.http.maxPayloadBytes == 0) {
            throw std::runtime_error("server.http.maxPayloadBytes must be positive");
        }
        if (opts.http.readTimeoutSeconds < 0 || opts.http.writeTimeoutSeconds < 0 || opts.http.threadCount < 0) {
            throw std::runtime_error("server.http timeout and thread settings must be non-negative");
        }
    }
}

} // namespace agent_memory
