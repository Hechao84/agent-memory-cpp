#pragma once

#include <string>

#include "agent_memory/model_client.h"
#include "model_config.h"

namespace agent_memory {

struct OpenAiModelConfig : public ModelConfig
{
    std::string organization;
};

class OpenAiModelClient : public ModelClient
{
public:
    explicit OpenAiModelClient(OpenAiModelConfig config);
    ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) override;

    std::string Endpoint() const;
    std::string BuildRequestBody(const std::string& prompt) const;
    static std::string ParseResponseBody(const std::string& body);

private:
    OpenAiModelConfig config_;
};

} // namespace agent_memory
