#include "runtime_services.h"

#include <memory>

#include "consolidation_service.h"
#include "context_builder.h"
#include "memory_update_writer.h"
#include "payload_service.h"
#include "rule_based_processor.h"
#include "runtime_paths.h"
#include "runtime_store_initializer.h"
#include "store.h"

namespace agent_memory {

RuntimeServices::~RuntimeServices() = default;

std::unique_ptr<RuntimeServices> CreateRuntimeServices(const MemoryConfig& config)
{
    std::string dataPath = ResolveRuntimeDataPath(config);
    auto services = std::make_unique<RuntimeServices>();

    services->store = CreateRuntimeStore(config);

    services->payloadService = std::make_unique<PayloadService>(config, dataPath, services->store.get());
    services->contextBuilder = std::make_unique<ContextBuilder>(config, services->store.get());
    services->longTermProcessor = std::make_unique<RuleBasedLongTermMemoryProcessor>();
    if (services->store) {
        services->memoryUpdateWriter = std::make_unique<MemoryUpdateWriter>(*services->store);
        services->consolidationService = std::make_unique<ConsolidationService>(*services->memoryUpdateWriter,
                                                                                services->longTermProcessor.get());
    }
    return services;
}

} // namespace agent_memory
