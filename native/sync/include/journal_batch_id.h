#pragma once

#include <string>
#include <string_view>

namespace tightrope::sync {

std::string generate_batch_id();
bool is_valid_batch_id(std::string_view batch_id);

} // namespace tightrope::sync
