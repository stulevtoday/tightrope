#include "usage_fetcher.h"

#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>
#include <glaze/glaze.hpp>

#include "net/outbound_proxy.h"
#include "text/ascii.h"

namespace tightrope::usage {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

constexpr std::string_view kDefaultUpstreamBaseUrl = "https://chatgpt.com/backend-api";
constexpr long kDefaultConnectTimeoutMs = 10000;
constexpr long kDefaultRequestTimeoutMs = 15000;

std::shared_ptr<UsageValidator>& usage_validator_override() {
    static auto* validator = new std::shared_ptr<UsageValidator>();
    return *validator;
}

std::shared_ptr<UsagePayloadFetcher>& usage_payload_fetcher_override() {
    static auto* fetcher = new std::shared_ptr<UsagePayloadFetcher>();
    return *fetcher;
}

std::mutex& usage_payload_cache_mutex() {
    static auto* cache_mutex = new std::mutex();
    return *cache_mutex;
}

std::unordered_map<std::string, UsagePayloadSnapshot>& usage_payload_cache() {
    static auto* cache = new std::unordered_map<std::string, UsagePayloadSnapshot>();
    return *cache;
}

std::string env_or_default(const char* key, const std::string_view fallback) {
    if (key == nullptr) {
        return std::string(fallback);
    }
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return std::string(fallback);
    }
    return std::string(raw);
}

long env_long_or_default(const char* key, const long fallback) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtol(raw, &end, 10);
    if (end == raw || parsed <= 0) {
        return fallback;
    }
    return parsed;
}

std::string trim_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string normalized_account_id(const std::string_view account_id) {
    return core::text::trim_ascii(account_id);
}

void cache_usage_payload_snapshot(const std::string_view account_id, const UsagePayloadSnapshot& snapshot) {
    const auto key = normalized_account_id(account_id);
    if (key.empty()) {
        return;
    }
    std::lock_guard<std::mutex> guard(usage_payload_cache_mutex());
    usage_payload_cache()[key] = snapshot;
}

std::string usage_url() {
    auto base = trim_trailing_slashes(env_or_default("TIGHTROPE_UPSTREAM_BASE_URL", kDefaultUpstreamBaseUrl));
    if (base.find("/backend-api") == std::string::npos) {
        base += "/backend-api";
    }
    return base + "/wham/usage";
}

size_t write_callback(char* ptr, const size_t size, const size_t nmemb, void* userdata) {
    if (ptr == nullptr || userdata == nullptr) {
        return 0;
    }
    auto* output = static_cast<std::string*>(userdata);
    output->append(ptr, size * nmemb);
    return size * nmemb;
}

std::optional<std::string> json_string(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::string extract_error_message_from_payload(const std::string& payload) {
    Json parsed;
    if (const auto ec = glz::read_json(parsed, payload); ec || !parsed.is_object()) {
        return core::text::trim_ascii(payload);
    }

    const auto& object = parsed.get_object();
    const auto error_it = object.find("error");
    if (error_it != object.end()) {
        if (error_it->second.is_object()) {
            const auto message = json_string(error_it->second.get_object(), "message");
            if (message.has_value() && !message->empty()) {
                return *message;
            }
            const auto description = json_string(error_it->second.get_object(), "error_description");
            if (description.has_value() && !description->empty()) {
                return *description;
            }
        } else if (error_it->second.is_string()) {
            const auto description = json_string(object, "error_description");
            if (description.has_value() && !description->empty()) {
                return *description;
            }
            return error_it->second.get_string();
        }
    }

    const auto message = json_string(object, "message");
    if (message.has_value() && !message->empty()) {
        return *message;
    }

    const auto description = json_string(object, "error_description");
    if (description.has_value() && !description->empty()) {
        return *description;
    }
    return {};
}

std::string extract_error_code_from_payload(const std::string& payload) {
    Json parsed;
    if (const auto ec = glz::read_json(parsed, payload); ec || !parsed.is_object()) {
        return {};
    }

    const auto& object = parsed.get_object();
    const auto error_it = object.find("error");
    if (error_it == object.end()) {
        return {};
    }
    if (error_it->second.is_object()) {
        return json_string(error_it->second.get_object(), "code").value_or(std::string{});
    }
    if (error_it->second.is_string()) {
        return error_it->second.get_string();
    }
    return {};
}

struct UsageHttpResult {
    bool transport_ok = false;
    int status_code = 0;
    std::string body;
    std::string message;
};

std::optional<bool> json_bool(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_boolean()) {
        return std::nullopt;
    }
    return it->second.get_boolean();
}

std::optional<double> json_number(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_number()) {
        return std::nullopt;
    }
    return it->second.get_number();
}

std::optional<int> json_int(const JsonObject& object, const std::string_view key) {
    const auto number = json_number(object, key);
    if (!number.has_value()) {
        return std::nullopt;
    }
    return static_cast<int>(*number);
}

std::optional<std::int64_t> json_int64(const JsonObject& object, const std::string_view key) {
    const auto number = json_number(object, key);
    if (!number.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*number);
}

std::optional<UsageWindowSnapshot> parse_window_snapshot(const Json& value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto& object = value.get_object();
    UsageWindowSnapshot snapshot;
    snapshot.used_percent = json_int(object, "used_percent");
    snapshot.limit_window_seconds = json_int(object, "limit_window_seconds");
    snapshot.reset_after_seconds = json_int(object, "reset_after_seconds");
    snapshot.reset_at = json_int64(object, "reset_at");

    if (!snapshot.used_percent.has_value() && !snapshot.limit_window_seconds.has_value() &&
        !snapshot.reset_after_seconds.has_value() && !snapshot.reset_at.has_value()) {
        return std::nullopt;
    }
    return snapshot;
}

bool limit_reached_from_windows(
    const std::optional<UsageWindowSnapshot>& primary,
    const std::optional<UsageWindowSnapshot>& secondary
) {
    return (primary.has_value() && primary->used_percent.has_value() && *primary->used_percent >= 100) ||
           (secondary.has_value() && secondary->used_percent.has_value() && *secondary->used_percent >= 100);
}

std::optional<UsageRateLimitDetails> parse_rate_limit_details(const Json& value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto& object = value.get_object();
    std::optional<UsageWindowSnapshot> primary;
    std::optional<UsageWindowSnapshot> secondary;
    if (const auto it = object.find("primary_window"); it != object.end()) {
        primary = parse_window_snapshot(it->second);
    }
    if (const auto it = object.find("secondary_window"); it != object.end()) {
        secondary = parse_window_snapshot(it->second);
    }

    const auto inferred_limit_reached = limit_reached_from_windows(primary, secondary);
    const auto limit_reached = json_bool(object, "limit_reached").value_or(inferred_limit_reached);
    const auto allowed = json_bool(object, "allowed").value_or(!limit_reached);

    if (!primary.has_value() && !secondary.has_value() && !json_bool(object, "allowed").has_value() &&
        !json_bool(object, "limit_reached").has_value()) {
        return std::nullopt;
    }

    return UsageRateLimitDetails{
        .allowed = allowed,
        .limit_reached = limit_reached,
        .primary_window = primary,
        .secondary_window = secondary,
    };
}

std::optional<UsageCreditsDetails> parse_credits_details(const Json& value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const auto& object = value.get_object();
    const auto has_credits = json_bool(object, "has_credits");
    const auto unlimited = json_bool(object, "unlimited");
    auto balance = json_string(object, "balance");
    if (!balance.has_value()) {
        const auto numeric_balance = json_number(object, "balance");
        if (numeric_balance.has_value()) {
            balance = std::to_string(*numeric_balance);
        }
    }

    if (!has_credits.has_value() && !unlimited.has_value() && !balance.has_value()) {
        return std::nullopt;
    }

    return UsageCreditsDetails{
        .has_credits = has_credits.value_or(false),
        .unlimited = unlimited.value_or(false),
        .balance = balance,
    };
}

std::vector<UsageAdditionalRateLimit> parse_additional_rate_limits(const Json& value) {
    std::vector<UsageAdditionalRateLimit> parsed;
    if (!value.is_array()) {
        return parsed;
    }

    for (const auto& item : value.get_array()) {
        if (!item.is_object()) {
            continue;
        }
        const auto& object = item.get_object();
        const auto limit_name = json_string(object, "limit_name");
        const auto metered_feature = json_string(object, "metered_feature");
        if (!limit_name.has_value() || !metered_feature.has_value() || limit_name->empty() || metered_feature->empty()) {
            continue;
        }

        std::optional<UsageRateLimitDetails> rate_limit;
        if (const auto it = object.find("rate_limit"); it != object.end()) {
            rate_limit = parse_rate_limit_details(it->second);
        }

        parsed.push_back(
            UsageAdditionalRateLimit{
                .quota_key = json_string(object, "quota_key"),
                .limit_name = *limit_name,
                .display_label = json_string(object, "display_label"),
                .metered_feature = *metered_feature,
                .rate_limit = std::move(rate_limit),
            }
        );
    }

    return parsed;
}

std::optional<UsagePayloadSnapshot> parse_usage_payload(const std::string& payload) {
    Json parsed;
    if (const auto ec = glz::read_json(parsed, payload); ec || !parsed.is_object()) {
        return std::nullopt;
    }
    const auto& object = parsed.get_object();

    UsagePayloadSnapshot snapshot;
    snapshot.plan_type = json_string(object, "plan_type").value_or("guest");
    if (snapshot.plan_type.empty()) {
        snapshot.plan_type = "guest";
    }

    if (const auto it = object.find("rate_limit"); it != object.end()) {
        snapshot.rate_limit = parse_rate_limit_details(it->second);
    }
    if (const auto it = object.find("credits"); it != object.end()) {
        snapshot.credits = parse_credits_details(it->second);
    }
    if (const auto it = object.find("additional_rate_limits"); it != object.end()) {
        snapshot.additional_rate_limits = parse_additional_rate_limits(it->second);
    }

    return snapshot;
}

UsageHttpResult fetch_usage_http(const std::string_view access_token, const std::string_view account_id) {
    if (access_token.empty()) {
        return {
            .transport_ok = true,
            .status_code = 401,
            .body = {},
            .message = "Missing ChatGPT token in Authorization header",
        };
    }

    static std::once_flag curl_once;
    std::call_once(curl_once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            fprintf(stderr, "[tightrope] curl_global_init failed; usage transport may be unavailable\n");
        }
    });

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return {
            .transport_ok = false,
            .status_code = 0,
            .body = {},
            .message = "Usage validator transport unavailable",
        };
    }
    std::string proxy_error;
    if (!core::net::apply_curl_outbound_proxy(curl, &proxy_error)) {
        curl_easy_cleanup(curl);
        return {
            .transport_ok = false,
            .status_code = 0,
            .body = {},
            .message = "Usage outbound proxy unavailable: " + proxy_error,
        };
    }

    std::string response_body;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    const auto auth_header = "Authorization: Bearer " + std::string(access_token);
    headers = curl_slist_append(headers, auth_header.c_str());
    if (!account_id.empty() && !account_id.starts_with("email_") && !account_id.starts_with("local_")) {
        const auto account_header = "chatgpt-account-id: " + std::string(account_id);
        headers = curl_slist_append(headers, account_header.c_str());
    }

    const auto connect_timeout_ms =
        env_long_or_default("TIGHTROPE_USAGE_FETCH_CONNECT_TIMEOUT_MS", kDefaultConnectTimeoutMs);
    const auto timeout_ms = env_long_or_default("TIGHTROPE_USAGE_FETCH_TIMEOUT_MS", kDefaultRequestTimeoutMs);
    const auto url = usage_url();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tightrope-native/0.1");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const auto curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK) {
        const auto message = std::string("Usage fetch failed: ") + curl_easy_strerror(curl_result);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return {
            .transport_ok = false,
            .status_code = 0,
            .body = {},
            .message = message,
        };
    }

    long status_code = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return {
        .transport_ok = true,
        .status_code = static_cast<int>(status_code),
        .body = std::move(response_body),
        .message = {},
    };
}

class CurlUsageValidator final : public UsageValidator {
public:
    [[nodiscard]] UsageValidationResult validate(
        const std::string_view access_token,
        const std::string_view account_id
    ) override {
        const auto http = fetch_usage_http(access_token, account_id);
        if (!http.transport_ok) {
            return {
                .success = false,
                .status_code = 0,
                .error_code = {},
                .message = http.message,
            };
        }
        if (http.status_code >= 400) {
            auto message = extract_error_message_from_payload(http.body);
            auto error_code = extract_error_code_from_payload(http.body);
            if (message.empty()) {
                message = !http.message.empty() ? http.message
                                                : ("Usage fetch failed (" + std::to_string(http.status_code) + ")");
            }
            return {
                .success = false,
                .status_code = http.status_code,
                .error_code = std::move(error_code),
                .message = message,
            };
        }
        return {
            .success = true,
            .status_code = http.status_code,
            .error_code = {},
            .message = {},
        };
    }
};

class CurlUsagePayloadFetcher final : public UsagePayloadFetcher {
public:
    [[nodiscard]] std::optional<UsagePayloadSnapshot> fetch(
        const std::string_view access_token,
        const std::string_view account_id
    ) override {
        const auto http = fetch_usage_http(access_token, account_id);
        if (!http.transport_ok || http.status_code < 200 || http.status_code >= 300) {
            return std::nullopt;
        }
        return parse_usage_payload(http.body);
    }
};

} // namespace

UsageValidationResult validate_usage_identity(
    const std::string_view access_token,
    const std::string_view account_id,
    std::shared_ptr<UsageValidator> validator
) {
    if (!validator) {
        validator = usage_validator_override();
    }
    if (!validator) {
        validator = std::make_shared<CurlUsageValidator>();
    }
    return validator->validate(access_token, account_id);
}

std::optional<UsagePayloadSnapshot> fetch_usage_payload(
    const std::string_view access_token,
    const std::string_view account_id,
    std::shared_ptr<UsagePayloadFetcher> fetcher
) {
    if (!fetcher) {
        fetcher = usage_payload_fetcher_override();
    }
    if (!fetcher) {
        fetcher = std::make_shared<CurlUsagePayloadFetcher>();
    }
    auto snapshot = fetcher->fetch(access_token, account_id);
    if (snapshot.has_value()) {
        cache_usage_payload_snapshot(account_id, *snapshot);
    }
    return snapshot;
}

std::optional<UsagePayloadSnapshot> cached_usage_payload(const std::string_view account_id) {
    const auto key = normalized_account_id(account_id);
    if (key.empty()) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> guard(usage_payload_cache_mutex());
    const auto it = usage_payload_cache().find(key);
    if (it == usage_payload_cache().end()) {
        return std::nullopt;
    }
    return it->second;
}

void set_usage_validator_for_testing(std::shared_ptr<UsageValidator> validator) {
    usage_validator_override() = std::move(validator);
}

void clear_usage_validator_for_testing() {
    usage_validator_override().reset();
}

void set_usage_payload_fetcher_for_testing(std::shared_ptr<UsagePayloadFetcher> fetcher) {
    usage_payload_fetcher_override() = std::move(fetcher);
}

void clear_usage_payload_fetcher_for_testing() {
    usage_payload_fetcher_override().reset();
}

void seed_usage_payload_cache_for_testing(const std::string_view account_id, std::optional<UsagePayloadSnapshot> payload) {
    const auto key = normalized_account_id(account_id);
    if (key.empty()) {
        return;
    }
    std::lock_guard<std::mutex> guard(usage_payload_cache_mutex());
    if (payload.has_value()) {
        usage_payload_cache()[key] = *payload;
        return;
    }
    usage_payload_cache().erase(key);
}

void clear_usage_payload_cache_for_testing() {
    std::lock_guard<std::mutex> guard(usage_payload_cache_mutex());
    usage_payload_cache().clear();
}

} // namespace tightrope::usage
