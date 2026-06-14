#pragma once

#include <string>

#include "agent_memory/model_client.h"
#include "model_config.h"

namespace agent_memory {

struct AnthropicModelConfig : public ModelConfig
{
    std::string anthropicVersion{"2023-06-01"};
};

class AnthropicModelClient : public ModelClient
{
public:
    explicit AnthropicModelClient(AnthropicModelConfig config);
    ModelInvokeResult GenerateMemoryUpdate(const std::string& prompt) override;

    std::string Endpoint() const;
    std::string BuildRequestBody(const std::string& prompt) const;
    static std::string ParseResponseBody(const std::string& body);

private:
    AnthropicModelConfig config_;
};

} // namespace agent_memory
