#pragma once

#include <string_view>

namespace agent_memory {

namespace context_sections {
inline constexpr std::string_view Messages = "messages";
inline constexpr std::string_view Payloads = "payloads";
inline constexpr std::string_view Payload = "payload";
inline constexpr std::string_view LongTerm = "long_term";
inline constexpr std::string_view LongTermMemory = "long_term_memory";
} // namespace context_sections

} // namespace agent_memory
