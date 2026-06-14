#include "file_util.h"

#include <fstream>
#include <iterator>

namespace agent_memory {

std::optional<std::string> LoadTextFile(const std::string& path, bool binary)
{
    std::ifstream file(path, binary ? std::ios::binary : std::ios::in);
    if (!file.is_open()) {
        return std::nullopt;
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 0) {
        return std::nullopt;
    }

    std::string result;
    result.reserve(static_cast<std::string::size_type>(size));
    result.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    if (file.fail() && !file.eof()) {
        return std::nullopt;
    }

    return result;
}

} // namespace agent_memory