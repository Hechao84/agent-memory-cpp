#pragma once

#include <memory>

#include "agent_memory/builtin_memory_runtime.h"
#include "agent_memory/model_client.h"
#include "server_options.h"

namespace agent_memory {

struct ServerSetup
{
    MemoryConfig config;
    std::unique_ptr<BuiltinMemoryRuntime> runtime;
    std::unique_ptr<ModelClient> model;
};

ServerSetup CreateServerSetup(const ServerOptions& options);

} // namespace agent_memory
