#pragma once
// SHA-256 per-entry verification

#include <string>
#include <string_view>

namespace tightrope::sync {

std::string sha256_hex(std::string_view input);
std::string journal_checksum(
    std::string_view table_name,
    std::string_view row_pk,
    std::string_view op,
    std::string_view old_values,
    std::string_view new_values
);

} // namespace tightrope::sync
