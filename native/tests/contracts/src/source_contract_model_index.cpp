#include "contracts/internal/source_contract_internal.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <stdexcept>
#include <unordered_set>

#include "contract_io.h"

namespace tightrope::tests::contracts::internal {

std::string trim_copy(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string to_upper_copy(const std::string& value) {
    std::string upper = value;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return upper;
}

std::string to_lower_copy(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower;
}

std::size_t find_matching_delimiter(
    const std::string& text,
    const std::size_t open_pos,
    const char open_char,
    const char close_char
) {
    if (open_pos >= text.size() || text[open_pos] != open_char) {
        return std::string::npos;
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    char quote_char = '\0';
    for (std::size_t i = open_pos; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == quote_char) {
                in_string = false;
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            in_string = true;
            quote_char = ch;
            continue;
        }
        if (ch == open_char) {
            ++depth;
            continue;
        }
        if (ch == close_char) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char ch : text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (ch != '\r') {
            current.push_back(ch);
        }
    }
    lines.push_back(current);
    return lines;
}

void push_unique(std::vector<std::string>& out, const std::string& value) {
    if (std::find(out.begin(), out.end(), value) == out.end()) {
        out.push_back(value);
    }
}

namespace {

std::vector<std::string> resolve_model_fields_recursive(
    const std::string& expr,
    const ModelIndex& index,
    std::unordered_set<std::string>& visited
) {
    std::vector<std::string> resolved;
    std::string trimmed = trim_copy(expr);
    if (trimmed.empty()) {
        return resolved;
    }

    const std::size_t pipe_pos = trimmed.find('|');
    if (pipe_pos != std::string::npos) {
        const auto left = resolve_model_fields_recursive(trimmed.substr(0, pipe_pos), index, visited);
        const auto right = resolve_model_fields_recursive(trimmed.substr(pipe_pos + 1), index, visited);
        for (const auto& field : left) {
            push_unique(resolved, field);
        }
        for (const auto& field : right) {
            push_unique(resolved, field);
        }
        return resolved;
    }

    if ((trimmed.rfind("list[", 0) == 0 || trimmed.rfind("List[", 0) == 0) && trimmed.back() == ']') {
        return resolve_model_fields_recursive(trimmed.substr(5, trimmed.size() - 6), index, visited);
    }

    const std::size_t dot = trimmed.rfind('.');
    if (dot != std::string::npos) {
        trimmed = trimmed.substr(dot + 1);
    }
    if (!visited.insert(trimmed).second) {
        return resolved;
    }

    if (const auto class_it = index.class_fields.find(trimmed); class_it != index.class_fields.end()) {
        for (const auto& field : class_it->second) {
            push_unique(resolved, field);
        }
    }
    if (const auto alias_it = index.aliases.find(trimmed); alias_it != index.aliases.end()) {
        for (const auto& alias_target : alias_it->second) {
            const auto nested = resolve_model_fields_recursive(alias_target, index, visited);
            for (const auto& field : nested) {
                push_unique(resolved, field);
            }
        }
    }
    return resolved;
}

void index_models_from_text(const std::string& text, ModelIndex& index) {
    const auto lines = split_lines(text);
    const std::regex class_pattern(R"(^class\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    const std::regex field_pattern(R"(^\s{4}([A-Za-z_][A-Za-z0-9_]*)\s*:)");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::smatch class_match;
        if (!std::regex_search(lines[i], class_match, class_pattern) || class_match.size() < 2) {
            continue;
        }

        const std::string class_name = class_match[1].str();
        std::vector<std::string> fields;
        std::size_t j = i + 1;
        for (; j < lines.size(); ++j) {
            const std::string& line = lines[j];
            if (!line.empty() && line.front() != ' ' && line.front() != '\t') {
                break;
            }
            std::smatch field_match;
            if (std::regex_search(line, field_match, field_pattern) && field_match.size() >= 2) {
                push_unique(fields, field_match[1].str());
            }
        }
        index.class_fields[class_name] = fields;
        i = (j == 0) ? 0 : j - 1;
    }

    const std::regex alias_pattern(R"((\w+)\s*:\s*TypeAlias\s*=\s*([^\n]+))");
    for (std::sregex_iterator it(text.begin(), text.end(), alias_pattern), end; it != end; ++it) {
        const std::string alias_name = trim_copy((*it)[1].str());
        const std::string alias_expr = trim_copy((*it)[2].str());
        if (!alias_name.empty() && !alias_expr.empty()) {
            index.aliases[alias_name].push_back(alias_expr);
        }
    }
}

} // namespace

std::vector<std::string> resolve_model_fields(const std::string& expr, const ModelIndex& index) {
    std::unordered_set<std::string> visited;
    return resolve_model_fields_recursive(expr, index, visited);
}

ModelIndex build_model_index(const std::filesystem::path& reference_repo_root) {
    const std::filesystem::path app_root = reference_repo_root / "app";
    if (!std::filesystem::exists(app_root)) {
        throw std::runtime_error("Reference app path not found: " + app_root.string());
    }

    ModelIndex index;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(app_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".py") {
            continue;
        }
        index_models_from_text(read_text_file(entry.path()), index);
    }
    return index;
}

} // namespace tightrope::tests::contracts::internal
