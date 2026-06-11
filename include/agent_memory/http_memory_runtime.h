#pragma once

#include <string>
#include <vector>

#include "agent_memory/runtime.h"

namespace agent_memory {

class HttpMemoryRuntime : public MemoryRuntime
{
public:
    explicit HttpMemoryRuntime(MemoryConfig config);

    bool AppendEvent(const MemoryEvent& event) override;
    MemoryContextPackage BuildContext(const MemoryContextRequest& request) override;
    MemoryPayloadWriteResult WritePayload(const MemoryPayloadWriteRequest& request) override;
    std::string ReadPayload(const std::string& ref) override;
    bool Consolidate(const MemoryConsolidationRequest& request) override;
    std::vector<MemorySearchResult> SearchMemory(const MemorySearchRequest& request) override;
    MemoryStats GetStats() const override;

private:
    struct HttpResponse
    {
        long status{0};
        std::string body;
    };

    HttpResponse Get(const std::string& path) const;
    HttpResponse Post(const std::string& path, const std::string& body) const;
    std::string UrlForPath(const std::string& path) const;
    std::string EncodeRefPath(const std::string& ref) const;

    std::string serverUrl_;
};

} // namespace agent_memory
