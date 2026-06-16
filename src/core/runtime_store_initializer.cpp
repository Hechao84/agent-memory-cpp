#include "runtime_store_initializer.h"

#include <filesystem>
#include <memory>

#include "runtime_paths.h"
#include "sqlite_store.h"

namespace fs = std::filesystem;

namespace agent_memory {

RuntimeStoreCreateResult CreateRuntimeStore(const MemoryConfig& config)
{
    fs::path dbPath = RuntimeDatabasePath(config);
    std::error_code error;
    fs::create_directories(dbPath.parent_path(), error);
    if (error) {
        return {nullptr, "failed to create runtime data directory: " + dbPath.parent_path().string() + ": " + error.message()};
    }

    auto store = std::make_unique<MemorySqliteStore>(dbPath.string());
    auto initializeResult = store->Initialize();
    if (!initializeResult) {
        std::string error = "failed to initialize sqlite memory store: " + dbPath.string();
        if (!initializeResult.error.details.empty()) {
            error += ": " + initializeResult.error.details;
        }
        return {nullptr, error};
    }
    return {std::move(store), ""};
}

} // namespace agent_memory
