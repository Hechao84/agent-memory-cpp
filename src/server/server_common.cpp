#include "server_common.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include "model_client_factory.h"

namespace fs = std::filesystem;

namespace agent_memory {

namespace {

fs::path BuildWriteProbePath(const fs::path& directory)
{
    static std::atomic<unsigned long long> counter{0};
    std::ostringstream name;
    name << ".agent_memory_write_test_" << std::this_thread::get_id() << "_" << counter.fetch_add(1, std::memory_order_relaxed);
    return directory / name.str();
}

std::string PrepareDataPath(const std::string& dataPath)
{
    std::error_code ec;
    fs::create_directories(dataPath, ec);
    if (ec) {
        throw std::runtime_error("failed to create dataPath: " + dataPath + ": " + ec.message());
    }

    fs::path canonical = fs::weakly_canonical(dataPath, ec);
    if (ec) {
        throw std::runtime_error("failed to resolve dataPath: " + dataPath + ": " + ec.message());
    }

    fs::path probe = BuildWriteProbePath(canonical);
    {
        std::ofstream file(probe.string(), std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("dataPath is not writable: " + canonical.string());
        }
    }
    fs::remove(probe, ec);
    if (ec) {
        throw std::runtime_error("failed to clean dataPath write test: " + canonical.string() + ": " + ec.message());
    }

    return canonical.string();
}

std::string LoadString(const nlohmann::json& j, const std::string& key, const std::string& defaultValue = "")
{
    if (!j.contains(key) || !j[key].is_string()) {
        return defaultValue;
    }
    return j[key].get<std::string>();
}

int LoadInt(const nlohmann::json& j, const std::string& key, int defaultValue)
{
    if (!j.contains(key) || !j[key].is_number_integer()) {
        return defaultValue;
    }
    return j[key].get<int>();
}

double LoadDouble(const nlohmann::json& j, const std::string& key, double defaultValue)
{
    if (!j.contains(key) || !j[key].is_number()) {
        return defaultValue;
    }
    return j[key].get<double>();
}

void LoadHeaders(const nlohmann::json& j, std::unordered_map<std::string, std::string>& headers)
{
    if (!j.contains("headers") || !j["headers"].is_object()) {
        return;
    }
    for (auto it = j["headers"].begin(); it != j["headers"].end(); ++it) {
        if (it.value().is_string()) {
            headers[it.key()] = it.value().get<std::string>();
        }
    }
}

MemoryModelConfig ModelConfigFromJson(const nlohmann::json& j)
{
    MemoryModelConfig config;
    if (!j.is_object() || j.empty()) {
        return config;
    }
    config.enabled = true;
    config.formatType = LoadString(j, "formatType", config.formatType);
    config.baseUrl = LoadString(j, "baseUrl");
    config.apiKey = LoadString(j, "apiKey");
    config.modelName = LoadString(j, "modelName");
    config.organization = LoadString(j, "organization");
    config.anthropicVersion = LoadString(j, "anthropicVersion", LoadString(j, "anthropic-version", config.anthropicVersion));
    config.timeoutSeconds = LoadInt(j, "timeoutSeconds", config.timeoutSeconds);
    config.temperature = LoadDouble(j, "temperature", config.temperature);
    config.maxTokens = LoadInt(j, "maxTokens", LoadInt(j, "max_tokens", config.maxTokens));
    if (j.contains("extraParams") && j["extraParams"].is_object()) {
        config.extraParams = j["extraParams"];
    }
    LoadHeaders(j, config.headers);
    return config;
}

} // namespace

ServerSetup CreateServerSetup(const ServerOptions& options)
{
    MemoryConfig config;
    config.dataPath = PrepareDataPath(options.dataPath);
    config.enablePayloadOffload = options.enablePayloadOffload;
    config.offloadThresholdChars = options.offloadThreshold;
    config.tokenBudget = options.tokenBudget;
    config.model = ModelConfigFromJson(options.modelConfig);
    if (config.model.enabled) {
        auto modelResult = LoadModelClientFromConfig(config.model);
        if (!modelResult && options.strictModelConfig) {
            throw std::runtime_error(modelResult.error);
        }
        if (!modelResult) {
            std::cerr << modelResult.error << ". Using rule-based consolidation.\n";
            config.model.enabled = false;
        }
    }

    auto runtime = std::make_unique<BuiltinMemoryRuntime>(config);

    return {std::move(config), std::move(runtime)};
}

} // namespace agent_memory
