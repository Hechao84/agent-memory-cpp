#pragma once

#include <memory>

#include "agent_memory/builtin_memory_runtime.h"
#include "server_options.h"

namespace agent_memory {

struct ServerSetup
{
    MemoryConfig config;
    std::unique_ptr<BuiltinMemoryRuntime> runtime;
};

ServerSetup CreateServerSetup(const ServerOptions& options);

} // namespace agent_memory
