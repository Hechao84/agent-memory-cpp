#pragma once

#include <memory>
#include <string>

#include "agent_memory/config.h"

namespace agent_memory {

class ConsolidationService;
class ContextBuilder;
class LongTermMemoryProcessor;
class MemoryStore;
class MemoryUpdateWriter;
class MemoryModelClient;
class PayloadService;

struct RuntimeServices
{
    ~RuntimeServices();

    std::unique_ptr<LongTermMemoryProcessor> longTermProcessor;
    std::unique_ptr<MemoryStore> store;
    std::unique_ptr<PayloadService> payloadService;
    std::unique_ptr<ContextBuilder> contextBuilder;
    std::unique_ptr<MemoryUpdateWriter> memoryUpdateWriter;
    std::unique_ptr<ConsolidationService> consolidationService;
    std::unique_ptr<MemoryModelClient> modelClient;
    std::string modelClientError;
    std::string storeError;
};

std::unique_ptr<RuntimeServices> CreateRuntimeServices(const MemoryConfig& config);

} // namespace agent_memory
