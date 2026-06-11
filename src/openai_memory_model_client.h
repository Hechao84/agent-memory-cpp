#pragma once

#include <string>

#include "agent_memory/model_client.h"

namespace agent_memory {

struct OpenAiMemoryModelConfig
{
    std::string baseUrl;
    std::string apiKey;
    std::string modelName;
    int timeoutSeconds{60};
};

class OpenAiMemoryModelClient : public MemoryModelClient
{
public:
    explicit OpenAiMemoryModelClient(OpenAiMemoryModelConfig config);
    std::string InvokeMemoryExtraction(const std::string& prompt) override;

private:
    struct HttpResponse
    {
        long status{0};
        std::string body;
    };

    HttpResponse PostJson(const std::string& body) const;
    std::string Endpoint() const;

    OpenAiMemoryModelConfig config_;
};

} // namespace agent_memory
