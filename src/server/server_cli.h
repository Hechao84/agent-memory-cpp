#pragma once

#include <string>

namespace agent_memory {

bool ParsePort(const std::string& value, int& port);
bool RequireCliValue(int i, int argc, const std::string& arg);
bool HasHelpOption(int argc, char* argv[]);
bool FindConfigPath(int argc, char* argv[], std::string& configPath);

} // namespace agent_memory
