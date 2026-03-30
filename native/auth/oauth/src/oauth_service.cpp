#include "oauth_service.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <glaze/glaze.hpp>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

#include "config_loader.h"
#include "connection/sqlite_pool.h"
#include "logging/logger.h"
#include "repositories/account_repo.h"

namespace tightrope::auth::oauth {

namespace {

using Clock = std::chrono::steady_clock;
using Json = glz::generic;

struct OAuthState {
    std::string status = "idle";
    std::optional<std::string> method;
    std::optional<std::string> error_message;
    std::optional<std::string> state_token;
    std::optional<std::string> code_verifier;
    std::optional<std::string> authorization_url;
    std::optional<std::string> callback_url;
    std::optional<std::string> verification_url;
    std::optional<std::string> device_auth_id;
    std::optional<std::string> user_code;
    std::optional<int> interval_seconds;
    std::optional<int> expires_in_seconds;
    std::optional<Clock::time_point> expires_at;
    std::string db_path;
    bool poll_active = false;
};

struct IdTokenClaims {
    std::optional<std::string> email;
    std::optional<std::string> account_id;
    std::optional<std::string> plan_type;
};

std::mutex& oauth_mutex() {
    static auto* mutex = new std::mutex();
    return *mutex;
}

OAuthState& oauth_state() {
    static auto* state = new OAuthState();
    return *state;
}

std::shared_ptr<ProviderClient>& provider_client_state() {
    static auto* provider = new std::shared_ptr<ProviderClient>(make_default_provider_client());
    return *provider;
}

std::jthread& poll_thread_state() {
    static auto* thread = new std::jthread();
    return *thread;
}

std::string random_token() {
    auto value = boost::uuids::to_string(boost::uuids::random_generator()());
    value.erase(std::remove(value.begin(), value.end(), '-'), value.end());
    return value;
}

std::string url_encode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[(c >> 4) & 0x0F]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

std::string base64_url_encode(const unsigned char* data, const std::size_t len) {
    std::size_t encoded_len = 0;
    (void)mbedtls_base64_encode(nullptr, 0, &encoded_len, data, len);
    std::string encoded(encoded_len, '\0');
    if (mbedtls_base64_encode(
            reinterpret_cast<unsigned char*>(encoded.data()),
            encoded.size(),
            &encoded_len,
            data,
            len
        ) != 0) {
        return {};
    }
    encoded.resize(encoded_len);
    for (char& ch : encoded) {
        if (ch == '+') {
            ch = '-';
        } else if (ch == '/') {
            ch = '_';
        }
    }
    while (!encoded.empty() && encoded.back() == '=') {
        encoded.pop_back();
    }
    return encoded;
}

std::optional<std::string> base64_url_decode(std::string value) {
    for (char& ch : value) {
        if (ch == '-') {
            ch = '+';
        } else if (ch == '_') {
            ch = '/';
        }
    }
    while ((value.size() % 4) != 0) {
        value.push_back('=');
    }
    std::size_t decoded_len = 0;
    (void)mbedtls_base64_decode(
        nullptr,
        0,
        &decoded_len,
        reinterpret_cast<const unsigned char*>(value.data()),
        value.size()
    );
    std::string decoded(decoded_len, '\0');
    if (mbedtls_base64_decode(
            reinterpret_cast<unsigned char*>(decoded.data()),
            decoded.size(),
            &decoded_len,
            reinterpret_cast<const unsigned char*>(value.data()),
            value.size()
        ) != 0) {
        return std::nullopt;
    }
    decoded.resize(decoded_len);
    return decoded;
}

std::string pkce_challenge(std::string_view verifier) {
    std::array<unsigned char, 32> digest{};
    if (mbedtls_sha256(
            reinterpret_cast<const unsigned char*>(verifier.data()),
            verifier.size(),
            digest.data(),
            0
        ) != 0) {
        return {};
    }
    return base64_url_encode(digest.data(), digest.size());
}

std::string build_authorization_url(const std::string& state, const std::string& challenge) {
    return oauth_auth_base_url() + "/oauth/authorize?response_type=code&client_id=" + url_encode(oauth_client_id()) +
           "&redirect_uri=" + url_encode(oauth_redirect_uri()) + "&scope=" + url_encode(oauth_scope()) +
           "&code_challenge=" + url_encode(challenge) + "&code_challenge_method=S256&state=" + url_encode(state) +
           "&id_token_add_organizations=true&codex_cli_simplified_flow=true&originator=" +
           url_encode(oauth_originator());
}

std::optional<std::string> json_string(const Json::object_t& payload, std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

IdTokenClaims parse_id_token_claims(std::string_view id_token) {
    IdTokenClaims claims;
    const auto first_dot = id_token.find('.');
    if (first_dot == std::string_view::npos) {
        return claims;
    }
    const auto second_dot = id_token.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos || second_dot <= first_dot + 1) {
        return claims;
    }

    const auto payload_part = std::string(id_token.substr(first_dot + 1, second_dot - first_dot - 1));
    const auto decoded = base64_url_decode(payload_part);
    if (!decoded.has_value()) {
        return claims;
    }

    Json payload;
    if (const auto ec = glz::read_json(payload, *decoded); ec || !payload.is_object()) {
        return claims;
    }
    const auto& payload_obj = payload.get_object();
    claims.email = json_string(payload_obj, "email");
    claims.account_id = json_string(payload_obj, "chatgpt_account_id");
    claims.plan_type = json_string(payload_obj, "chatgpt_plan_type");

    const auto auth_it = payload_obj.find("https://api.openai.com/auth");
    if (auth_it != payload_obj.end() && auth_it->second.is_object()) {
        const auto& auth_obj = auth_it->second.get_object();
        if (!claims.account_id.has_value()) {
            claims.account_id = json_string(auth_obj, "chatgpt_account_id");
        }
        if (!claims.plan_type.has_value()) {
            claims.plan_type = json_string(auth_obj, "chatgpt_plan_type");
        }
    }

    return claims;
}

int decode_hex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

std::string percent_decode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '+') {
            decoded.push_back(' ');
            continue;
        }
        if (ch == '%' && i + 2 < value.size()) {
            const int upper = decode_hex(value[i + 1]);
            const int lower = decode_hex(value[i + 2]);
            if (upper >= 0 && lower >= 0) {
                decoded.push_back(static_cast<char>((upper << 4) | lower));
                i += 2;
                continue;
            }
        }
        decoded.push_back(ch);
    }
    return decoded;
}

std::map<std::string, std::string> parse_query(std::string_view query) {
    std::map<std::string, std::string> fields;
    std::size_t cursor = 0;
    while (cursor < query.size()) {
        const auto next_amp = query.find('&', cursor);
        const auto token_end = next_amp == std::string_view::npos ? query.size() : next_amp;
        const auto token = query.substr(cursor, token_end - cursor);
        const auto eq = token.find('=');
        if (eq == std::string_view::npos) {
            fields.emplace(percent_decode(token), "");
        } else {
            fields.emplace(percent_decode(token.substr(0, eq)), percent_decode(token.substr(eq + 1)));
        }
        if (next_amp == std::string_view::npos) {
            break;
        }
        cursor = next_amp + 1;
    }
    return fields;
}

std::map<std::string, std::string> parse_callback_url(std::string_view callback_url) {
    const auto question = callback_url.find('?');
    if (question == std::string_view::npos || question + 1 >= callback_url.size()) {
        return {};
    }
    return parse_query(callback_url.substr(question + 1));
}

std::string html_error(std::string_view message) {
    return std::string("<!doctype html><html><head><meta charset=\"utf-8\"><title>OAuth Error</title></head><body><h1>"
    ) + std::string(message) + "</h1></body></html>";
}

std::string html_success() {
    return R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>OAuth Success</title>
</head>
<body>
  <h1>Authorization complete. You can close this tab.</h1>
  <p>Returning to Tightrope…</p>
  <p>If the app does not open automatically, <a href="tightrope://oauth/success">click here</a>.</p>
  <script>
    (function () {
      try {
        window.location.href = "tightrope://oauth/success";
      } catch (error) {
        // ignore deep-link launch errors; fallback link remains visible
      }
    })();
    window.setTimeout(function () {
      window.close();
    }, 300);
  </script>
</body>
</html>
)HTML";
}

std::string db_path_for_oauth(sqlite3* db) {
    if (db != nullptr) {
        if (const char* filename = sqlite3_db_filename(db, "main"); filename != nullptr && filename[0] != '\0') {
            return filename;
        }
    }
    auto config = config::load_config();
    return config.db_path.empty() ? std::string("store.db") : config.db_path;
}

bool has_existing_accounts(sqlite3* db, const std::string& db_path) {
    if (db != nullptr) {
        return !db::list_accounts(db).empty();
    }
    db::SqlitePool pool(db_path);
    if (!pool.open() || pool.connection() == nullptr) {
        return false;
    }
    return !db::list_accounts(pool.connection()).empty();
}

bool persist_tokens(sqlite3* db, const std::string& db_path, const OAuthTokens& tokens) {
    const auto claims = parse_id_token_claims(tokens.id_token);
    const std::string email = claims.email.value_or("unknown@example.com");

    db::OauthAccountUpsert upsert;
    upsert.email = email;
    upsert.provider = "openai";
    upsert.chatgpt_account_id = claims.account_id;
    upsert.plan_type = claims.plan_type;
    upsert.access_token_encrypted = tokens.access_token;
    upsert.refresh_token_encrypted = tokens.refresh_token;
    upsert.id_token_encrypted = tokens.id_token;

    if (db != nullptr) {
        return db::upsert_oauth_account(db, upsert).has_value();
    }

    db::SqlitePool pool(db_path);
    if (!pool.open() || pool.connection() == nullptr) {
        return false;
    }
    return db::upsert_oauth_account(pool.connection(), upsert).has_value();
}

void set_success_locked() {
    auto& state = oauth_state();
    state.status = "success";
    state.error_message.reset();
}

void set_error_locked(std::string message) {
    auto& state = oauth_state();
    state.status = "error";
    state.error_message = std::move(message);
}

void stop_poll_thread() {
    std::jthread thread_to_join;
    {
        std::lock_guard lock(oauth_mutex());
        auto& poll_thread = poll_thread_state();
        if (poll_thread.joinable()) {
            poll_thread.request_stop();
            thread_to_join = std::move(poll_thread);
        }
        oauth_state().poll_active = false;
    }
}

void reset_state() {
    stop_poll_thread();
    std::lock_guard lock(oauth_mutex());
    oauth_state() = OAuthState{};
}

std::shared_ptr<ProviderClient> active_provider() {
    std::lock_guard lock(oauth_mutex());
    if (!provider_client_state()) {
        provider_client_state() = make_default_provider_client();
    }
    return provider_client_state();
}

void run_device_poll_loop(const std::string db_path, sqlite3* db_override, const std::stop_token stop_token) {
    auto provider = active_provider();
    while (!stop_token.stop_requested()) {
        std::string device_auth_id;
        std::string user_code;
        int interval_seconds = 0;
        Clock::time_point expires_at{};
        {
            std::lock_guard lock(oauth_mutex());
            auto& state = oauth_state();
            if (!state.device_auth_id.has_value() || !state.user_code.has_value() || !state.expires_at.has_value()) {
                set_error_locked("Device code flow is not initialized.");
                state.poll_active = false;
                return;
            }
            device_auth_id = *state.device_auth_id;
            user_code = *state.user_code;
            interval_seconds = std::max(0, state.interval_seconds.value_or(0));
            expires_at = *state.expires_at;
        }

        if (Clock::now() >= expires_at) {
            std::lock_guard lock(oauth_mutex());
            set_error_locked("Device code expired.");
            oauth_state().poll_active = false;
            return;
        }

        const auto poll_result = provider->exchange_device_token({
            .device_auth_id = device_auth_id,
            .user_code = user_code,
        });

        if (poll_result.kind == DeviceTokenPollKind::pending) {
            if (interval_seconds > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }

        if (poll_result.kind == DeviceTokenPollKind::error) {
            std::lock_guard lock(oauth_mutex());
            const auto message = poll_result.error.has_value() ? poll_result.error->message : "Device OAuth failed";
            set_error_locked(message);
            oauth_state().poll_active = false;
            return;
        }

        ProviderResult<OAuthTokens> tokens_result = ProviderResult<OAuthTokens>::fail({
            .code = "invalid_response",
            .message = "OAuth response missing tokens",
            .status_code = 0,
        });
        if (poll_result.kind == DeviceTokenPollKind::tokens && poll_result.tokens.has_value()) {
            tokens_result = ProviderResult<OAuthTokens>::ok(*poll_result.tokens);
        } else if (
            poll_result.kind == DeviceTokenPollKind::authorization_code && poll_result.authorization_code.has_value() &&
            poll_result.code_verifier.has_value()
        ) {
            tokens_result = provider->exchange_authorization_code({
                .code = *poll_result.authorization_code,
                .code_verifier = *poll_result.code_verifier,
                .redirect_uri = oauth_auth_base_url() + "/deviceauth/callback",
            });
        }

        if (!tokens_result.is_ok()) {
            std::lock_guard lock(oauth_mutex());
            set_error_locked(tokens_result.error.has_value() ? tokens_result.error->message : "Device OAuth failed");
            oauth_state().poll_active = false;
            return;
        }

        if (!persist_tokens(db_override, db_path, *tokens_result.value)) {
            std::lock_guard lock(oauth_mutex());
            set_error_locked("Failed to persist OAuth account.");
            oauth_state().poll_active = false;
            return;
        }

        std::lock_guard lock(oauth_mutex());
        set_success_locked();
        oauth_state().poll_active = false;
        return;
    }
}

} // namespace

OauthService::OauthService() = default;

OauthService::~OauthService() {
    reset_state();
}

OauthService& OauthService::instance() {
    static OauthService service;
    return service;
}

StartResponse OauthService::start(const StartRequest& request, sqlite3* db) {
    const auto force_method = request.force_method.value_or("");
    std::string normalized_method = force_method;
    std::transform(normalized_method.begin(), normalized_method.end(), normalized_method.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const auto db_path = db_path_for_oauth(db);

    if (normalized_method != "device" && normalized_method.empty() && has_existing_accounts(db, db_path)) {
        std::lock_guard lock(oauth_mutex());
        auto& state = oauth_state();
        state = OAuthState{};
        state.status = "success";
        state.method = "browser";
        return {
            .status = 200,
            .method = "browser",
        };
    }

    if (normalized_method == "device") {
        auto provider = active_provider();
        const auto result = provider->request_device_code();
        if (!result.is_ok()) {
            return {
                .status = 502,
                .code = result.error->code,
                .message = result.error->message,
            };
        }

        std::lock_guard lock(oauth_mutex());
        auto& state = oauth_state();
        state = OAuthState{};
        state.status = "pending";
        state.method = "device";
        state.device_auth_id = result.value->device_auth_id;
        state.user_code = result.value->user_code;
        state.verification_url = result.value->verification_url;
        state.interval_seconds = result.value->interval_seconds;
        state.expires_in_seconds = result.value->expires_in_seconds;
        state.expires_at = Clock::now() + std::chrono::seconds(std::max(1, result.value->expires_in_seconds));
        state.db_path = db_path;

        return {
            .status = 200,
            .method = "device",
            .verification_url = state.verification_url,
            .user_code = state.user_code,
            .device_auth_id = state.device_auth_id,
            .interval_seconds = state.interval_seconds,
            .expires_in_seconds = state.expires_in_seconds,
        };
    }

    stop_poll_thread();
    const auto state_token = random_token();
    const auto code_verifier = random_token() + random_token();
    const auto code_challenge = pkce_challenge(code_verifier);
    const auto callback_url = oauth_redirect_uri();
    const auto authorization_url = build_authorization_url(state_token, code_challenge);

    std::lock_guard lock(oauth_mutex());
    auto& state = oauth_state();
    state = OAuthState{};
    state.status = "pending";
    state.method = "browser";
    state.state_token = state_token;
    state.code_verifier = code_verifier;
    state.callback_url = callback_url;
    state.authorization_url = authorization_url;
    state.db_path = db_path;

    return {
        .status = 200,
        .method = "browser",
        .authorization_url = authorization_url,
        .callback_url = callback_url,
    };
}

StatusResponse OauthService::status() {
    std::lock_guard lock(oauth_mutex());
    const auto& state = oauth_state();
    const auto status = state.status == "idle" ? "pending" : state.status;
    const bool listener_running = state.method == std::optional<std::string>{"browser"} && state.status == "pending";
    return {
        .status = 200,
        .oauth_status = status,
        .error_message = state.error_message,
        .listener_running = listener_running,
        .callback_url = state.callback_url,
        .authorization_url = state.authorization_url,
    };
}

StatusResponse OauthService::stop() {
    stop_poll_thread();
    std::lock_guard lock(oauth_mutex());
    auto& state = oauth_state();
    if (state.method == std::optional<std::string>{"browser"} || state.method == std::optional<std::string>{"device"}) {
        state.status = "stopped";
        state.error_message.reset();
    }
    state.state_token.reset();
    state.code_verifier.reset();

    const auto status = state.status == "idle" ? "pending" : state.status;
    const bool listener_running = state.method == std::optional<std::string>{"browser"} && state.status == "pending";
    return {
        .status = 200,
        .oauth_status = status,
        .error_message = state.error_message,
        .listener_running = listener_running,
        .callback_url = state.callback_url,
        .authorization_url = state.authorization_url,
    };
}

StartResponse OauthService::restart(sqlite3* db) {
    StartRequest request{};
    request.force_method = "browser";
    return start(request, db);
}

CompleteResponse OauthService::complete(const CompleteRequest& request, sqlite3* db) {
    const auto db_path = db_path_for_oauth(db);
    {
        std::lock_guard lock(oauth_mutex());
        auto& state = oauth_state();
        if (request.device_auth_id.has_value()) {
            state.device_auth_id = request.device_auth_id;
        }
        if (request.user_code.has_value()) {
            state.user_code = request.user_code;
        }

        if (state.status == "success") {
            return {.status = 200, .oauth_status = "success"};
        }
        if (state.method != std::optional<std::string>{"device"}) {
            return {.status = 200, .oauth_status = "pending"};
        }
        if (state.poll_active) {
            return {.status = 200, .oauth_status = "pending"};
        }
        if (!state.device_auth_id.has_value() || !state.user_code.has_value() || !state.expires_at.has_value()) {
            set_error_locked("Device code flow is not initialized.");
            return {
                .status = 200,
                .code = "device_not_initialized",
                .message = "Device code flow is not initialized.",
                .oauth_status = "error",
            };
        }
        if (Clock::now() >= *state.expires_at) {
            set_error_locked("Device code expired.");
            return {
                .status = 200,
                .code = "device_expired",
                .message = "Device code expired.",
                .oauth_status = "error",
            };
        }
        state.poll_active = true;
        state.db_path = db_path;
    }

    auto& poll_thread = poll_thread_state();
    if (poll_thread.joinable()) {
        poll_thread.request_stop();
        poll_thread.join();
    }
    poll_thread = std::jthread([db_path](const std::stop_token token) {
        run_device_poll_loop(db_path, nullptr, token);
    });

    return {
        .status = 200,
        .oauth_status = "pending",
    };
}

ManualCallbackResponse OauthService::manual_callback(const std::string_view callback_url, sqlite3* db) {
    const auto parsed = parse_callback_url(callback_url);
    const auto error_it = parsed.find("error");
    if (error_it != parsed.end() && !error_it->second.empty()) {
        const auto message = "OAuth error: " + error_it->second;
        std::lock_guard lock(oauth_mutex());
        set_error_locked(message);
        return {.status = 200, .oauth_status = "error", .error_message = message};
    }

    const auto code_it = parsed.find("code");
    const auto state_it = parsed.find("state");

    std::optional<std::string> expected_state;
    std::optional<std::string> verifier;
    std::string db_path;
    {
        std::lock_guard lock(oauth_mutex());
        expected_state = oauth_state().state_token;
        verifier = oauth_state().code_verifier;
        db_path = oauth_state().db_path;
    }

    const bool valid = code_it != parsed.end() && !code_it->second.empty() && state_it != parsed.end() &&
                       !state_it->second.empty() && expected_state.has_value() && verifier.has_value() &&
                       state_it->second == *expected_state;
    if (!valid) {
        const std::string message = "Invalid OAuth callback: state mismatch or missing code.";
        std::lock_guard lock(oauth_mutex());
        set_error_locked(message);
        return {.status = 200, .oauth_status = "error", .error_message = message};
    }

    auto provider = active_provider();
    const auto exchange = provider->exchange_authorization_code({
        .code = code_it->second,
        .code_verifier = *verifier,
        .redirect_uri = oauth_redirect_uri(),
    });
    if (!exchange.is_ok()) {
        std::lock_guard lock(oauth_mutex());
        set_error_locked(exchange.error->message);
        return {.status = 200, .oauth_status = "error", .error_message = exchange.error->message};
    }
    if (!persist_tokens(db, db_path, *exchange.value)) {
        const std::string message = "Failed to persist OAuth account.";
        std::lock_guard lock(oauth_mutex());
        set_error_locked(message);
        return {.status = 200, .oauth_status = "error", .error_message = message};
    }

    std::lock_guard lock(oauth_mutex());
    set_success_locked();
    return {
        .status = 200,
        .oauth_status = "success",
        .error_message = std::nullopt,
    };
}

BrowserCallbackResponse OauthService::browser_callback(
    const std::string_view code,
    const std::string_view state,
    const std::string_view error,
    sqlite3* db
) {
    if (!error.empty()) {
        std::lock_guard lock(oauth_mutex());
        set_error_locked("OAuth error: " + std::string(error));
        return {.status = 200, .body = html_error("Authorization failed.")};
    }

    std::optional<std::string> expected_state;
    std::optional<std::string> verifier;
    std::string db_path;
    {
        std::lock_guard lock(oauth_mutex());
        expected_state = oauth_state().state_token;
        verifier = oauth_state().code_verifier;
        db_path = oauth_state().db_path;
    }
    const bool valid = !code.empty() && !state.empty() && expected_state.has_value() && verifier.has_value() &&
                       state == *expected_state;
    if (!valid) {
        std::lock_guard lock(oauth_mutex());
        set_error_locked("Invalid OAuth callback state.");
        return {.status = 200, .body = html_error("Invalid OAuth callback.")};
    }

    auto provider = active_provider();
    const auto exchange = provider->exchange_authorization_code({
        .code = std::string(code),
        .code_verifier = *verifier,
        .redirect_uri = oauth_redirect_uri(),
    });
    if (!exchange.is_ok()) {
        std::lock_guard lock(oauth_mutex());
        set_error_locked(exchange.error->message);
        return {.status = 200, .body = html_error(exchange.error->message)};
    }
    if (!persist_tokens(db, db_path, *exchange.value)) {
        std::lock_guard lock(oauth_mutex());
        set_error_locked("Failed to persist OAuth account.");
        return {.status = 200, .body = html_error("Failed to persist OAuth account.")};
    }

    std::lock_guard lock(oauth_mutex());
    set_success_locked();
    return {.status = 200, .body = html_success()};
}

void OauthService::set_provider_client_for_testing(std::shared_ptr<ProviderClient> provider) {
    std::lock_guard lock(oauth_mutex());
    provider_client_state() = provider;
}

void OauthService::clear_provider_client_for_testing() {
    std::lock_guard lock(oauth_mutex());
    provider_client_state() = make_default_provider_client();
}

void OauthService::reset_for_testing() {
    reset_state();
}

void set_provider_client_for_testing(std::shared_ptr<ProviderClient> provider) {
    OauthService::instance().set_provider_client_for_testing(std::move(provider));
}

void clear_provider_client_for_testing() {
    OauthService::instance().clear_provider_client_for_testing();
}

void reset_oauth_state_for_testing() {
    OauthService::instance().reset_for_testing();
}

} // namespace tightrope::auth::oauth
