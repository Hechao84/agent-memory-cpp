#pragma once

#include <string>

#include "agent_memory/model_client.h"
#include "model_config.h"
#include "model_http_client.h"

namespace agent_memory {

struct OpenAiModelConfig : public ModelConfig
{
    std::string organization;
};

class OpenAiModelClient : public ModelClient
{
public:
    explicit OpenAiModelClient(OpenAiModelConfig config);
    OpenAiModelClient(OpenAiModelConfig config, ModelHttpClient httpClient);
    ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) override;

    std::string Endpoint() const;
    std::string BuildRequestBody(const std::string& prompt) const;
    static std::string ParseResponseBody(const std::string& body);

private:
    OpenAiModelConfig config_;
    ModelHttpClient httpClient_;
};

} // namespace agent_memory
