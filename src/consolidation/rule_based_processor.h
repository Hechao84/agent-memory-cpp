#pragma once

#include <string>

#include "long_term_memory_processor.h"

namespace agent_memory {

class RuleBasedLongTermMemoryProcessor : public LongTermMemoryProcessor
{
public:
    LongTermMemoryUpdate Process(const LongTermMemoryBatch& batch) override;

private:
    bool ContainsPreferenceSignal(const std::string& text) const;
    std::string DetectTopic(const std::string& text) const;
    std::string ToLower(const std::string& text) const;
};

} // namespace agent_memory
