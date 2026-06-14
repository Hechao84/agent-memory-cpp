#pragma once

#include <string>

namespace agent_memory {

[[nodiscard]] std::string CanonicalPath(const std::string& path);
[[nodiscard]] bool IsPathInsideDirectory(const std::string& path, const std::string& directory);

} // namespace agent_memory