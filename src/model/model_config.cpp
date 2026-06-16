#include "model_config.h"

#include "json_helpers.h"

namespace agent_memory {

ModelConfig LoadModelConfig(const nlohmann::json& j, int defaultMaxTokens)
{
    ModelConfig config;
    config.baseUrl = JsonString(j, "baseUrl");
    config.apiKey = JsonString(j, "apiKey");
    config.modelName = JsonString(j, "modelName");
    config.timeoutSeconds = JsonInt(j, "timeoutSeconds", 60);
    config.temperature = JsonDouble(j, "temperature", 0.0);
    config.maxTokens = JsonInt(j, "maxTokens", JsonInt(j, "max_tokens", defaultMaxTokens));
    if (j.contains("extraParams") && j["extraParams"].is_object()) {
        config.extraParams = j["extraParams"];
    }
    config.headers = JsonStringMap(j, "headers");
    return config;
}

} // namespace agent_memory
