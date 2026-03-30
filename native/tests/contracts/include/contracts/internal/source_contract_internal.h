#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tightrope::tests::contracts::internal {

struct ModelIndex {
    std::unordered_map<std::string, std::vector<std::string>> class_fields;
    std::unordered_map<std::string, std::vector<std::string>> aliases;
};

std::string trim_copy(const std::string& value);
std::string to_upper_copy(const std::string& value);
std::string to_lower_copy(const std::string& value);
std::size_t find_matching_delimiter(const std::string& text, std::size_t open_pos, char open_char, char close_char);
std::vector<std::string> split_lines(const std::string& text);
void push_unique(std::vector<std::string>& out, const std::string& value);
std::vector<std::string> resolve_model_fields(const std::string& expr, const ModelIndex& index);
ModelIndex build_model_index(const std::filesystem::path& reference_repo_root);

} // namespace tightrope::tests::contracts::internal
