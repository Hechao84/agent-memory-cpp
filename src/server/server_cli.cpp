#include "server_cli.h"

#include <charconv>
#include <iostream>
#include <system_error>

namespace agent_memory {

bool ParsePort(const std::string& value, int& port)
{
    int parsed = 0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last || parsed < 1 || parsed > 65535) {
        return false;
    }
    port = parsed;
    return true;
}

bool RequireCliValue(int i, int argc, const std::string& arg)
{
    if (i + 1 < argc) {
        return true;
    }
    std::cerr << arg << " requires a value\n";
    return false;
}

bool HasHelpOption(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") {
            return true;
        }
    }
    return false;
}

bool FindConfigPath(int argc, char* argv[], std::string& configPath)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") {
            if (!RequireCliValue(i, argc, arg)) {
                return false;
            }
            configPath = argv[++i];
        }
    }
    return true;
}

} // namespace agent_memory
