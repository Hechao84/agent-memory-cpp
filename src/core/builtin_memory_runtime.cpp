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
    mutable std::mutex mutex;
    mutable std::mutex consolidationMutex;
};

MemoryStore* GetStore(BuiltinMemoryRuntimeImpl& impl)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    return impl.services->store.get();
}

ContextBuilder* GetContextBuilder(BuiltinMemoryRuntimeImpl& impl)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    return impl.services->contextBuilder.get();
}

PayloadService* GetPayloadService(BuiltinMemoryRuntimeImpl& impl)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    return impl.services->payloadService.get();
}

ConsolidationService* GetConsolidationService(BuiltinMemoryRuntimeImpl& impl)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    return impl.services->consolidationService.get();
}

ModelClient* GetConfiguredModel(BuiltinMemoryRuntimeImpl& impl)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    return impl.services->modelClient.get();
}

std::string StoreUnavailableMessage(BuiltinMemoryRuntimeImpl& impl)
{
    std::lock_guard<std::mutex> lock(impl.mutex);
    return impl.services->storeError.empty() ? "memory store unavailable" : impl.services->storeError;
}

MemoryConsolidationResult ConsolidateWithModel(BuiltinMemoryRuntimeImpl& impl,
                                              const MemoryConsolidationRequest& request,
                                              ModelClient* model)
{
    ConsolidationService* consolidationService = GetConsolidationService(impl);
    MemoryStore* store = GetStore(impl);

    if (store == nullptr) {
        MemoryConsolidationResult result;
        result.error = {"store_unavailable", StoreUnavailableMessage(impl), "", false};
        return result;
    }
    if (consolidationService == nullptr) {
        MemoryConsolidationResult result;
        result.error = {"consolidation_unavailable", "consolidation service unavailable", "", false};
        return result;
    }

    std::lock_guard<std::mutex> consolidationLock(impl.consolidationMutex);
    std::string startCursor = request.forceReprocess ? std::string() : store->LoadConsolidationCursor(request.agentId, request.sessionId);
    std::vector<MemoryEvent> events = store->LoadEventsAfterCursor(request.agentId, request.sessionId, startCursor);
    MemoryConsolidationResult result = consolidationService->Consolidate(request, events, model);
    if (result.succeeded && store != nullptr && !result.nextCursor.empty() &&
        !store->SaveConsolidationCursor(request.agentId, request.sessionId, result.nextCursor)) {
        result.succeeded = false;
        result.error = {"cursor_save_failed", "failed to persist consolidation cursor", "", false};
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
    MemoryStore* store = GetStore(*impl_);
    if (store == nullptr) {
        return MemoryFailure("store_unavailable", StoreUnavailableMessage(*impl_));
    }
    if (!store->SaveEvent(event)) {
        return MemoryFailure("event_persist_failed", "failed to persist memory event");
    }
    return MemorySuccess();
}

MemoryContextResult BuiltinMemoryRuntime::BuildContext(const MemoryContextRequest& request)
{
    ContextBuilder* contextBuilder = GetContextBuilder(*impl_);
    if (contextBuilder == nullptr) {
        return {false, {}, {"context_build_failed", "context builder unavailable", "", false}};
    }
    return {true, contextBuilder->BuildContext(request), {}};
}

MemoryPayloadWriteResult BuiltinMemoryRuntime::WritePayload(const MemoryPayloadWriteRequest& request)
{
    PayloadService* payloadService = GetPayloadService(*impl_);
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
    PayloadService* payloadService = GetPayloadService(*impl_);
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
    return ConsolidateWithModel(*impl_, request, GetConfiguredModel(*impl_));
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
    MemoryStore* store = GetStore(*impl_);
    if (store == nullptr) {
        return {false, {}, {"search_unavailable", StoreUnavailableMessage(*impl_), "", false}};
    }
    return {true, store->SearchLongTermMemory(request), {}};
}

MemoryStatsResult BuiltinMemoryRuntime::GetStats() const
{
    MemoryStore* store = GetStore(*impl_);
    if (store == nullptr) {
        return {false, {}, {"stats_unavailable", StoreUnavailableMessage(*impl_), "", false}};
    }
    return {true, store->GetStoreStats(), {}};
}

} // namespace agent_memory
