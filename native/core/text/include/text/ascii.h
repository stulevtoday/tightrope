#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace tightrope::core::text {

inline std::string to_lower_ascii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const auto ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

inline bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

inline std::string trim_ascii(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(start, end - start));
}

inline bool equals_case_insensitive(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto left = static_cast<unsigned char>(lhs[index]);
        const auto right = static_cast<unsigned char>(rhs[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

template <typename MapLike>
bool has_key_case_insensitive(const MapLike& map, std::string_view wanted) {
    const auto wanted_lower = to_lower_ascii(wanted);
    for (const auto& [key, value] : map) {
        static_cast<void>(value);
        if (to_lower_ascii(key) == wanted_lower) {
            return true;
        }
    }
    return false;
}

template <typename MapLike>
std::optional<std::string_view> find_value_case_insensitive(const MapLike& map, std::string_view wanted) {
    const auto wanted_lower = to_lower_ascii(wanted);
    for (const auto& [key, value] : map) {
        if (to_lower_ascii(key) == wanted_lower) {
            return std::string_view(value);
        }
    }
    return std::nullopt;
}

} // namespace tightrope::core::text
