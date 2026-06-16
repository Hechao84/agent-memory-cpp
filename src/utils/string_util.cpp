#include "string_util.h"

#include <algorithm>
#include <cctype>

namespace agent_memory {

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool EqualsIgnoreCase(const std::string& left, const std::string& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    return ToLower(left) == ToLower(right);
}

} // namespace agent_memory
