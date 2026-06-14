#pragma once

#include <string>
#include <vector>

#include "agent_memory/model_client.h"
#include "long_term_memory_processor.h"

namespace agent_memory {

class LlmLongTermMemoryProcessor : public LongTermMemoryProcessor
{
public:
    explicit LlmLongTermMemoryProcessor(ModelClient* model);
    LongTermMemoryUpdate Process(const LongTermMemoryBatch& batch) override;

private:
    std::string BuildPrompt(const LongTermMemoryBatch& batch) const;
    LongTermMemoryUpdate ParseResponse(const std::string& jsonStr, const std::vector<std::string>& sourceRefs,
                                     const std::string& agentId) const;
    ModelClient* model_;
};

} // namespace agent_memory
