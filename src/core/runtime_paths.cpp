#include "runtime_paths.h"

namespace agent_memory {

std::string ResolveRuntimeDataPath(const MemoryConfig& config)
{
    if (!config.dataPath.empty()) {
        return config.dataPath;
    }
    return "./data";
}

std::filesystem::path RuntimeDatabasePath(const MemoryConfig& config)
{
    return std::filesystem::path(ResolveRuntimeDataPath(config)) / "memory_runtime" / "memory.db";
}

} // namespace agent_memory
