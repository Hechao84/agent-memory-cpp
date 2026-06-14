#pragma once

#include <filesystem>
#include <string>

#include "agent_memory/config.h"

namespace agent_memory {

std::string ResolveRuntimeDataPath(const MemoryConfig& config);
std::filesystem::path RuntimeDatabasePath(const MemoryConfig& config);

} // namespace agent_memory
