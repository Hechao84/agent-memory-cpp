#include "server_common.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
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

} // namespace

ServerSetup CreateServerSetup(const ServerOptions& options)
{
    std::unique_ptr<ModelClient> model;
    if (!options.modelConfig.empty()) {
        auto modelResult = LoadModelClientFromJson(options.modelConfig);
        if (!modelResult && options.strictModelConfig) {
            throw std::runtime_error(modelResult.error);
        }
        if (!modelResult) {
            std::cerr << modelResult.error << ". Using rule-based consolidation.\n";
        }
        model = std::move(modelResult.client);
    }

    MemoryConfig config;
    config.dataPath = PrepareDataPath(options.dataPath);
    config.enablePayloadOffload = options.enablePayloadOffload;
    config.offloadThresholdChars = options.offloadThreshold;
    config.tokenBudget = options.tokenBudget;

    auto runtime = std::make_unique<BuiltinMemoryRuntime>(config);

    return {std::move(config), std::move(runtime), std::move(model)};
}

} // namespace agent_memory
