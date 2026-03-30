#include "contract_io.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "text/json_escape.h"

namespace tightrope::tests::contracts {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open file for write: " + path.string());
    }
    output << text;
}

std::string escape_json_string(const std::string& value) {
    return tightrope::core::text::escape_json_string(value);
}

std::string quote_json_string(const std::string& value) {
    return tightrope::core::text::quote_json_string(value);
}

} // namespace tightrope::tests::contracts
