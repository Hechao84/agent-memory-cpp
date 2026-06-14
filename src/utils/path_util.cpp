#include "path_util.h"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace agent_memory {

std::string CanonicalPath(const std::string& path)
{
    std::error_code ec;
    auto result = fs::weakly_canonical(path, ec);
    if (ec) {
        return {};
    }
    return result.string();
}

bool IsPathInsideDirectory(const std::string& path, const std::string& directory)
{
    std::error_code ec;
    fs::path canonicalPath = fs::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }
    fs::path canonicalDirectory = fs::weakly_canonical(directory, ec);
    if (ec) {
        return false;
    }

    if (canonicalPath == canonicalDirectory) {
        return false;
    }

    auto dirIter = canonicalDirectory.begin();
    auto pathIter = canonicalPath.begin();
    while (dirIter != canonicalDirectory.end() && pathIter != canonicalPath.end()) {
        if (*dirIter != *pathIter) {
            return false;
        }
        ++dirIter;
        ++pathIter;
    }
    if (dirIter != canonicalDirectory.end()) {
        return false;
    }
    return true;
}

} // namespace agent_memory