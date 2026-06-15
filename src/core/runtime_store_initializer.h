#pragma once

#include <memory>
#include <string>

#include "agent_memory/config.h"

namespace agent_memory {

class MemoryStore;

struct RuntimeStoreCreateResult
{
    std::unique_ptr<MemoryStore> store;
    std::string error;

    explicit operator bool() const { return store != nullptr; }
};

RuntimeStoreCreateResult CreateRuntimeStore(const MemoryConfig& config);

} // namespace agent_memory
