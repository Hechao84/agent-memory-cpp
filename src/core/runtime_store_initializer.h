#pragma once

#include <memory>

#include "agent_memory/config.h"

namespace agent_memory {

class MemoryStore;

std::unique_ptr<MemoryStore> CreateRuntimeStore(const MemoryConfig& config);

} // namespace agent_memory
