#pragma once

#include <optional>
#include <string>

namespace agent_memory {

[[nodiscard]] std::optional<std::string> LoadTextFile(const std::string& path, bool binary = false);

} // namespace agent_memory