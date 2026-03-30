#include "source_contract_catalog.h"

#include <algorithm>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "contract_io.h"
#include "contracts/internal/source_contract_internal.h"

namespace tightrope::tests::contracts {

namespace {

struct RouterSpec {
    std::string prefix;
    std::string auth_mode;
};

std::string detect_auth_mode(const std::string& router_args) {
    if (router_args.find("validate_proxy_api_key") != std::string::npos) {
        return "proxy_api_key";
    }
    if (router_args.find("validate_dashboard_session") != std::string::npos) {
        return "dashboard_session";
    }
    if (router_args.find("validate_codex_usage_identity") != std::string::npos) {
        return "codex_usage_identity";
    }
    return "none";
}

std::string extract_named_string_arg(const std::string& args, const std::string& name) {
    const std::regex pattern(name + "\\s*=\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(args, match, pattern) || match.size() < 2) {
        return {};
    }
    return match[1].str();
}

std::optional<std::string> extract_first_string_literal(const std::string& text) {
    bool escaped = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '"') {
            continue;
        }
        std::string value;
        escaped = false;
        for (std::size_t j = i + 1; j < text.size(); ++j) {
            const char ch = text[j];
            if (escaped) {
                value.push_back(ch);
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                return value;
            }
            value.push_back(ch);
        }
        break;
    }
    return std::nullopt;
}

std::string join_route_path(const std::string& prefix, const std::string& suffix) {
    if (prefix.empty()) {
        return suffix.empty() ? "/" : suffix;
    }
    if (suffix.empty()) {
        return prefix;
    }
    std::string result = prefix;
    if (result.back() == '/' && suffix.front() == '/') {
        result.pop_back();
    } else if (result.back() != '/' && suffix.front() != '/') {
        result.push_back('/');
    }
    result += suffix;
    return result;
}

std::unordered_map<std::string, RouterSpec> parse_router_specs(const std::string& text) {
    std::unordered_map<std::string, RouterSpec> specs;
    const std::regex start_pattern(R"((\w+)\s*=\s*APIRouter\s*\()");
    for (std::sregex_iterator it(text.begin(), text.end(), start_pattern), end; it != end; ++it) {
        const std::string name = (*it)[1].str();
        const std::size_t open_pos = static_cast<std::size_t>(it->position(0) + it->length(0) - 1);
        const std::size_t close_pos = internal::find_matching_delimiter(text, open_pos, '(', ')');
        if (close_pos == std::string::npos) {
            continue;
        }

        const std::string args = text.substr(open_pos + 1, close_pos - open_pos - 1);
        specs[name] = RouterSpec{
            .prefix = extract_named_string_arg(args, "prefix"),
            .auth_mode = detect_auth_mode(args),
        };
    }
    return specs;
}

std::string extract_response_model_expr(const std::string& decorator_args) {
    const std::regex pattern(R"(response_model\s*=\s*([^,\)\n]+))");
    std::smatch match;
    if (!std::regex_search(decorator_args, match, pattern) || match.size() < 2) {
        return {};
    }
    return internal::trim_copy(match[1].str());
}

std::vector<std::string> extract_return_dict_keys(const std::string& file_text, const std::size_t start_pos) {
    const std::size_t next_decorator = file_text.find("\n@", start_pos);
    const std::size_t return_pos = file_text.find("return {", start_pos);
    if (return_pos == std::string::npos || (next_decorator != std::string::npos && return_pos > next_decorator)) {
        return {};
    }

    const std::size_t open_pos = file_text.find('{', return_pos);
    if (open_pos == std::string::npos) {
        return {};
    }
    const std::size_t close_pos = internal::find_matching_delimiter(file_text, open_pos, '{', '}');
    if (close_pos == std::string::npos) {
        return {};
    }

    const std::string object_text = file_text.substr(open_pos, close_pos - open_pos + 1);
    const std::regex key_pattern("\"([A-Za-z_][A-Za-z0-9_]*)\"\\s*:");
    std::vector<std::string> keys;
    for (std::sregex_iterator it(object_text.begin(), object_text.end(), key_pattern), end; it != end; ++it) {
        internal::push_unique(keys, (*it)[1].str());
    }
    return keys;
}

std::vector<SourceRouteContract> parse_route_contracts_from_api_file(
    const std::filesystem::path& path,
    const internal::ModelIndex& model_index
) {
    const std::string text = read_text_file(path);
    const auto routers = parse_router_specs(text);

    std::vector<SourceRouteContract> routes;
    const std::regex decorator_pattern(R"(@(\w+)\.(get|post|put|patch|delete|options)\s*\()");
    for (std::sregex_iterator it(text.begin(), text.end(), decorator_pattern), end; it != end; ++it) {
        const std::string router_name = (*it)[1].str();
        const std::string method = internal::to_upper_copy((*it)[2].str());
        const std::size_t open_pos = static_cast<std::size_t>(it->position(0) + it->length(0) - 1);
        const std::size_t close_pos = internal::find_matching_delimiter(text, open_pos, '(', ')');
        if (close_pos == std::string::npos) {
            continue;
        }

        const std::string args = text.substr(open_pos + 1, close_pos - open_pos - 1);
        const auto raw_route_path = extract_first_string_literal(args);
        if (!raw_route_path.has_value()) {
            continue;
        }

        const auto router_it = routers.find(router_name);
        const std::string prefix = (router_it != routers.end()) ? router_it->second.prefix : std::string{};
        const std::string auth_mode = (router_it != routers.end()) ? router_it->second.auth_mode : std::string("none");

        const std::string response_model = extract_response_model_expr(args);
        std::vector<std::string> response_body_keys = internal::resolve_model_fields(response_model, model_index);
        if (response_body_keys.empty()) {
            response_body_keys = extract_return_dict_keys(text, close_pos);
        }

        routes.push_back(SourceRouteContract{
            .method = method,
            .path = join_route_path(prefix, *raw_route_path),
            .auth_mode = auth_mode,
            .response_model = response_model,
            .response_body_keys = response_body_keys,
            .source_file = path,
        });
    }
    return routes;
}

} // namespace

std::vector<SourceRouteContract> load_source_route_contracts(const std::filesystem::path& reference_repo_root) {
    const std::filesystem::path modules_root = reference_repo_root / "app" / "modules";
    if (!std::filesystem::exists(modules_root)) {
        throw std::runtime_error("Reference modules path not found: " + modules_root.string());
    }

    const internal::ModelIndex model_index = internal::build_model_index(reference_repo_root);
    std::vector<SourceRouteContract> routes;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(modules_root)) {
        if (!entry.is_regular_file() || entry.path().filename() != "api.py") {
            continue;
        }
        auto parsed = parse_route_contracts_from_api_file(entry.path(), model_index);
        routes.insert(routes.end(), parsed.begin(), parsed.end());
    }

    std::sort(routes.begin(), routes.end(), [](const SourceRouteContract& a, const SourceRouteContract& b) {
        return std::tie(a.path, a.method) < std::tie(b.path, b.method);
    });
    return routes;
}

} // namespace tightrope::tests::contracts
