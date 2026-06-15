#include "agent_memory/builtin_memory_runtime.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "consolidation_service.h"
#include "context_builder.h"
#include "payload_service.h"
#include "runtime_services.h"
#include "store.h"


namespace agent_memory {

struct BuiltinMemoryRuntimeImpl
{
    std::unique_ptr<RuntimeServices> services;
    std::string lastRuntimeError;
    mutable std::mutex mutex;
    mutable std::mutex consolidationMutex;
};

MemoryConsolidationResult ConsolidateWithModel(BuiltinMemoryRuntimeImpl& impl,
                                              const MemoryConsolidationRequest& request,
                                              ModelClient* model)
{
    ConsolidationService* consolidationService = nullptr;
    MemoryStore* store = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        consolidationService = impl.services->consolidationService.get();
        store = impl.services->store.get();
    }

    if (consolidationService == nullptr) {
        MemoryConsolidationResult result;
        result.error = {"consolidation_unavailable", "consolidation service unavailable", "", false};
        return result;
    }

    std::lock_guard<std::mutex> consolidationLock(impl.consolidationMutex);
    std::string startCursor = request.forceReprocess || store == nullptr ? std::string() : store->LoadConsolidationCursor(request.agentId, request.sessionId);
    std::vector<MemoryEvent> events = store
        ? store->LoadEventsAfterCursor(request.agentId, request.sessionId, startCursor)
        : std::vector<MemoryEvent>();
    MemoryConsolidationResult result = consolidationService->Consolidate(request, events, model);
    if (result.succeeded && store != nullptr && !store->SaveConsolidationCursor(request.agentId, request.sessionId, result.nextCursor)) {
        result.succeeded = false;
        result.error = {"cursor_save_failed", "failed to persist consolidation cursor", "", false};
        std::lock_guard<std::mutex> lock(impl.mutex);
        impl.lastRuntimeError = result.error.message;
    }
    return result;
}

BuiltinMemoryRuntime::BuiltinMemoryRuntime(MemoryConfig config)
    : MemoryRuntime(std::move(config)), impl_(std::make_unique<BuiltinMemoryRuntimeImpl>())
{
    impl_->services = CreateRuntimeServices(config_);
}

BuiltinMemoryRuntime::~BuiltinMemoryRuntime() = default;

MemoryOperationResult BuiltinMemoryRuntime::AppendEvent(const MemoryEvent& event)
{
    MemoryStore* store = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        store = impl_->services->store.get();
    }
    if (store == nullptr) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->lastRuntimeError = "memory store unavailable";
        return MemoryFailure("store_unavailable", "memory store unavailable");
    }
    if (!store->SaveEvent(event)) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->lastRuntimeError = "failed to persist memory event";
        return MemoryFailure("event_persist_failed", "failed to persist memory event");
    }
    return MemorySuccess();
}

MemoryContextResult BuiltinMemoryRuntime::BuildContext(const MemoryContextRequest& request)
{
    ContextBuilder* contextBuilder = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        contextBuilder = impl_->services->contextBuilder.get();
    }
    if (contextBuilder == nullptr) {
        return {false, {}, {"context_build_failed", "context builder unavailable", "", false}};
    }
    return {true, contextBuilder->BuildContext(request), {}};
}

MemoryPayloadWriteResult BuiltinMemoryRuntime::WritePayload(const MemoryPayloadWriteRequest& request)
{
    PayloadService* payloadService = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        payloadService = impl_->services->payloadService.get();
    }
    if (payloadService == nullptr) {
        MemoryPayloadWriteResult result;
        result.replacementContent = request.content;
        result.error = {"payload_write_failed", "payload service unavailable", "", false};
        return result;
    }

    return payloadService->WritePayload(request);
}

MemoryPayloadReadResult BuiltinMemoryRuntime::ReadPayload(const std::string& uri)
{
    PayloadService* payloadService = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        payloadService = impl_->services->payloadService.get();
    }
    if (payloadService == nullptr) {
        return {false, std::string(), {"payload_read_failed", "payload service unavailable", "", false}};
    }
    std::string content = payloadService->ReadPayload(uri);
    if (content.empty()) {
        return {false, std::string(), {"payload_read_failed", "payload not found or empty", "", false}};
    }
    return {true, content, {}};
}

MemoryConsolidationResult BuiltinMemoryRuntime::Consolidate(const MemoryConsolidationRequest& request)
{
    ModelClient* model = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        model = impl_->services->modelClient.get();
    }
    return ConsolidateWithModel(*impl_, request, model);
}

MemoryConsolidationResult BuiltinMemoryRuntime::Consolidate(const MemoryConsolidationRequest& request, ModelClient* model)
{
    return ConsolidateWithModel(*impl_, request, model);
}

MemoryModelStatus BuiltinMemoryRuntime::GetModelStatus() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    MemoryModelStatus status;
    status.configured = config_.model.enabled;
    status.available = impl_->services->modelClient != nullptr;
    status.formatType = config_.model.formatType;
    status.modelName = config_.model.modelName;
    status.error = impl_->services->modelClientError;
    return status;
}

MemorySearchResponse BuiltinMemoryRuntime::SearchMemory(const MemorySearchRequest& request)
{
    std::vector<MemorySearchResult> results;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        results = impl_->services->store ? impl_->services->store->SearchLongTermMemory(request) : std::vector<MemorySearchResult>();
    }
    return {true, results, {}};
}

MemoryStatsResult BuiltinMemoryRuntime::GetStats() const
{
    MemoryStore* store = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        store = impl_->services->store.get();
    }
    if (store == nullptr) {
        return {false, {}, {"stats_unavailable", "memory store unavailable", "", false}};
    }
    return {true, store->GetStoreStats(), {}};
}

} // namespace agent_memory
