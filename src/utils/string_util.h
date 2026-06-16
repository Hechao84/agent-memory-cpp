#pragma once

#include <string>

namespace agent_memory {

std::string ToLower(std::string value);
bool EqualsIgnoreCase(const std::string& left, const std::string& right);

} // namespace agent_memory
