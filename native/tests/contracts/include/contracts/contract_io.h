#pragma once

#include <filesystem>
#include <string>

namespace tightrope::tests::contracts {

std::string read_text_file(const std::filesystem::path& path);

void write_text_file(const std::filesystem::path& path, const std::string& text);

std::string escape_json_string(const std::string& value);

std::string quote_json_string(const std::string& value);

} // namespace tightrope::tests::contracts
