#include "upstream_transport.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <curl/curl.h>
#include <glaze/glaze.hpp>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#if TIGHTROPE_HAS_ZSTD_DECOMPRESSION
#include <zstd.h>
#endif

#ifdef _WIN32
// Windows: OpenSSL has no default system cert path, so set_default_verify_paths()
// leaves the trust store empty and every TLS handshake fails with
// "unable to get local issuer certificate". Pull roots from the Windows cert store.
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

#include "internal/proxy_error_policy.h"
#include "internal/proxy_service_helpers.h"
#include "logging/logger.h"
#include "openai/error_envelope.h"
#include "text/ascii.h"
#include "account_traffic.h"

namespace tightrope::proxy {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

constexpr std::string_view kDefaultUpstreamBaseUrl = "https://chatgpt.com/backend-api";
constexpr long kDefaultConnectTimeoutMs = 10000;
constexpr long kDefaultRequestTimeoutMs = 120000;
constexpr std::string_view kUserAgent = "tightrope-native/0.1";
// Maximum cumulative bytes from upstream (HTTP response body or WebSocket event stream).
constexpr std::size_t kMaxUpstreamResponseBytes = 256ULL * 1024 * 1024;
// Maximum bytes for a single WebSocket frame from upstream.
constexpr std::size_t kMaxUpstreamFrameBytes = 32ULL * 1024 * 1024;

// On Windows, supplement OpenSSL's (empty) default verify store with roots from
// the Windows "ROOT" certificate store. Safe to call after set_default_verify_paths.
// No-op on other platforms — they already have usable defaults.
void load_system_root_certificates([[maybe_unused]] ssl::context& ctx) {
#ifdef _WIN32
    HCERTSTORE store = ::CertOpenSystemStoreW(0, L"ROOT");
    if (store == nullptr) {
        return;
    }
    X509_STORE* x509_store = SSL_CTX_get_cert_store(ctx.native_handle());
    if (x509_store == nullptr) {
        ::CertCloseStore(store, 0);
        return;
    }
    PCCERT_CONTEXT cert = nullptr;
    std::size_t added = 0;
    while ((cert = ::CertEnumCertificatesInStore(store, cert)) != nullptr) {
        const unsigned char* encoded = cert->pbCertEncoded;
        X509* x509 = d2i_X509(nullptr, &encoded, static_cast<long>(cert->cbCertEncoded));
        if (x509 != nullptr) {
            if (X509_STORE_add_cert(x509_store, x509) == 1) {
                ++added;
            }
            X509_free(x509);
        }
    }
    ::CertCloseStore(store, 0);
    static std::once_flag logged_once;
    std::call_once(logged_once, [added] {
        core::logging::log_event(
            core::logging::LogLevel::Info,
            "runtime",
            "proxy",
            "tls_root_store_loaded",
            "source=windows_root count=" + std::to_string(added)
        );
    });
#endif
}

struct ParsedUpstreamUrl {
    std::string scheme;
    std::string host;
    std::string port;
    std::string base_path;
};

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
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw || parsed <= 0) {
        return fallback;
    }
    return parsed;
}

bool env_flag_or_default(const char* key, const bool fallback = false) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(std::string_view(raw)));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

std::string lower_ascii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );
    return value;
}

std::string trim_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string build_upstream_url(const std::string_view path) {
    const auto base = trim_trailing_slashes(env_or_default("TIGHTROPE_UPSTREAM_BASE_URL", kDefaultUpstreamBaseUrl));
    if (path.empty()) {
        return base;
    }
    if (path.front() == '/') {
        return base + std::string(path);
    }
    return base + "/" + std::string(path);
}

std::string default_port_for_scheme(const std::string_view scheme) {
    if (scheme == "https" || scheme == "wss") {
        return "443";
    }
    return "80";
}

std::string join_upstream_path(const std::string& base_path, const std::string_view path) {
    std::string normalized_base = base_path.empty() ? std::string{} : base_path;
    if (!normalized_base.empty() && normalized_base.back() == '/') {
        normalized_base.pop_back();
    }
    std::string normalized_path = path.empty() ? std::string{} : std::string(path);
    if (normalized_path.empty()) {
        normalized_path = "/";
    }
    if (normalized_path.front() != '/') {
        normalized_path.insert(normalized_path.begin(), '/');
    }

    if (normalized_base.empty()) {
        return normalized_path;
    }
    return normalized_base + normalized_path;
}

std::optional<ParsedUpstreamUrl> parse_upstream_url(const std::string& url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos || scheme_end == 0) {
        return std::nullopt;
    }

    ParsedUpstreamUrl parsed;
    parsed.scheme = lower_ascii(url.substr(0, scheme_end));
    const auto rest = std::string_view(url).substr(scheme_end + 3);
    if (rest.empty()) {
        return std::nullopt;
    }

    const auto path_pos = rest.find('/');
    const auto authority = path_pos == std::string_view::npos ? rest : rest.substr(0, path_pos);
    parsed.base_path = path_pos == std::string_view::npos ? std::string{} : std::string(rest.substr(path_pos));
    if (authority.empty()) {
        return std::nullopt;
    }

    if (authority.front() == '[') {
        const auto closing = authority.find(']');
        if (closing == std::string_view::npos || closing <= 1) {
            return std::nullopt;
        }
        parsed.host = std::string(authority.substr(1, closing - 1));
        if (closing + 1 < authority.size() && authority[closing + 1] == ':') {
            parsed.port = std::string(authority.substr(closing + 2));
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            parsed.host = std::string(authority.substr(0, colon));
            parsed.port = std::string(authority.substr(colon + 1));
        } else {
            parsed.host = std::string(authority);
        }
    }

    if (parsed.host.empty()) {
        return std::nullopt;
    }
    if (parsed.port.empty()) {
        parsed.port = default_port_for_scheme(parsed.scheme);
    }
    if (parsed.base_path.empty()) {
        parsed.base_path = "";
    }
    return parsed;
}

bool websocket_stream_event_terminal(const std::string_view payload) {
    // Fast pre-filter: all terminal event types contain one of these substrings.
    // Avoids full JSON parse on every non-terminal frame.
    const bool candidate =
        payload.find("response.completed") != std::string_view::npos ||
        payload.find("response.incomplete") != std::string_view::npos ||
        payload.find("response.failed") != std::string_view::npos ||
        payload.find("\"error\"") != std::string_view::npos;
    if (!candidate) {
        return false;
    }
    Json parsed;
    if (const auto ec = glz::read_json(parsed, payload); ec || !parsed.is_object()) {
        return false;
    }
    const auto& object = parsed.get_object();
    const auto type_it = object.find("type");
    if (type_it == object.end() || !type_it->second.is_string()) {
        return false;
    }
    const auto type = type_it->second.get_string();
    return type == "response.completed" || type == "response.incomplete" || type == "response.failed" || type == "error";
}

curl_slist* append_header(curl_slist* headers, std::string_view key, std::string_view value);

bool websocket_header_is_hop_by_hop(const std::string_view lowered_key) {
    return lowered_key == "connection" || lowered_key == "upgrade" || lowered_key == "host" ||
           lowered_key == "sec-websocket-key" || lowered_key == "sec-websocket-version" ||
           lowered_key == "sec-websocket-extensions" || lowered_key == "content-length";
}

template <typename RequestT>
void apply_websocket_request_headers(RequestT& request, const openai::HeaderMap& plan_headers) {
    bool has_user_agent = false;
    for (const auto& [key, value] : plan_headers) {
        const auto lowered = lower_ascii(key);
        if (websocket_header_is_hop_by_hop(lowered)) {
            continue;
        }
        if (lowered == "user-agent") {
            has_user_agent = true;
        }
        request.set(key, value);
    }
    if (!has_user_agent) {
        request.set(beast::http::field::user_agent, kUserAgent);
    }
}

bool is_websocket_timeout(const boost::system::error_code& ec) {
    return ec == beast::error::timeout || ec == asio::error::timed_out;
}

std::optional<std::string> json_string_field(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::string extract_code_or_type(const JsonObject& object) {
    const auto code = json_string_field(object, "code");
    const auto error_type = json_string_field(object, "type");
    if (!code.has_value() && !error_type.has_value()) {
        return {};
    }
    return internal::normalize_upstream_error_code(
        code.value_or(std::string{}),
        error_type.value_or(std::string{})
    );
}

std::string normalize_ws_payload_for_http_fallback(const std::string& payload_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, payload_body); ec || !payload.is_object()) {
        return payload_body;
    }

    auto object = payload.get_object();
    if (const auto type = json_string_field(object, "type"); type.has_value() && *type == "response.create") {
        object.erase("type");
    }
    if (object.find("stream") == object.end()) {
        object["stream"] = true;
    }

    const auto serialized = glz::write_json(object);
    if (!serialized) {
        return payload_body;
    }
    return serialized.value_or(payload_body);
}

void capture_response_header_line(openai::HeaderMap& headers, std::string_view line) {
    if (line.empty() || line == "\r\n") {
        return;
    }
    if (core::text::starts_with(line, "HTTP/")) {
        return;
    }
    const auto separator = line.find(':');
    if (separator == std::string_view::npos || separator == 0) {
        return;
    }

    auto name = core::text::trim_ascii(line.substr(0, separator));
    auto value = core::text::trim_ascii(line.substr(separator + 1));
    if (!name.empty() && !value.empty() && value.back() == '\r') {
        value.pop_back();
        value = core::text::trim_ascii(value);
    }
    if (name.empty()) {
        return;
    }
    headers[std::string(name)] = std::string(value);
}

struct HttpWriteContext {
    std::string* output = nullptr;
    std::string traffic_account_id;
    bool overflowed = false;
};

size_t write_callback(char* ptr, const size_t size, const size_t nmemb, void* userdata) {
    if (ptr == nullptr || userdata == nullptr) {
        return 0;
    }
    const auto bytes = size * nmemb;
    auto* context = static_cast<HttpWriteContext*>(userdata);
    if (context->output == nullptr) {
        return 0;
    }
    if (context->output->size() + bytes > kMaxUpstreamResponseBytes) {
        context->overflowed = true;
        return 0; // Returning 0 causes CURLE_WRITE_ERROR, aborting the transfer.
    }
    context->output->append(ptr, bytes);
    if (!context->traffic_account_id.empty()) {
        record_account_upstream_ingress(context->traffic_account_id, bytes);
    }
    return bytes;
}

size_t header_callback(char* buffer, const size_t size, const size_t nitems, void* userdata) {
    if (buffer == nullptr || userdata == nullptr) {
        return 0;
    }
    const auto bytes = size * nitems;
    auto* headers = static_cast<openai::HeaderMap*>(userdata);
    capture_response_header_line(*headers, std::string_view(buffer, bytes));
    return bytes;
}

curl_slist* append_header(curl_slist* headers, const std::string_view key, const std::string_view value) {
    const auto line = std::string(key) + ": " + std::string(value);
    return curl_slist_append(headers, line.c_str());
}

bool is_codex_path(const std::string_view path) {
    return core::text::starts_with(path, "/codex/");
}

bool should_apply_request_compression(const openai::UpstreamRequestPlan& plan, const bool websocket_fallback) {
    if (websocket_fallback || plan.transport == "http-multipart" || plan.body.empty()) {
        return false;
    }
    if (!is_codex_path(plan.path)) {
        return false;
    }
    return env_flag_or_default("TIGHTROPE_UPSTREAM_ENABLE_REQUEST_COMPRESSION", false);
}

openai::HeaderMap capture_websocket_handshake_headers(const websocket::response_type& response) {
    openai::HeaderMap headers;
    for (const auto& field : response.base()) {
        headers[std::string(field.name_string())] = std::string(field.value());
    }
    return headers;
}

#if TIGHTROPE_HAS_ZSTD_DECOMPRESSION
std::optional<std::string> compress_request_body_zstd(const std::string& input) {
    if (input.empty()) {
        return std::string{};
    }
    const auto max_size = ZSTD_compressBound(input.size());
    std::string compressed(max_size, '\0');
    const auto size = ZSTD_compress(compressed.data(), compressed.size(), input.data(), input.size(), 3);
    if (ZSTD_isError(size) != 0) {
        return std::nullopt;
    }
    compressed.resize(size);
    return compressed;
}
#else
std::optional<std::string> compress_request_body_zstd(const std::string& input) {
    return std::nullopt;
}
#endif

std::vector<std::string> parse_sse_events(const std::string& body) {
    std::vector<std::string> events;
    std::string current;

    std::size_t start = 0;
    while (start <= body.size()) {
        const auto end = body.find('\n', start);
        std::string_view line = end == std::string::npos ? std::string_view(body).substr(start)
                                                          : std::string_view(body).substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (line.empty()) {
            if (!current.empty()) {
                events.push_back(current);
                current.clear();
            }
        } else if (line.starts_with("data:")) {
            auto data = core::text::trim_ascii(line.substr(5));
            if (data == "[DONE]") {
                break;
            }
            if (!data.empty()) {
                if (!current.empty()) {
                    current.push_back('\n');
                }
                current.append(data.data(), data.size());
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    if (!current.empty()) {
        events.push_back(current);
    }
    return events;
}

std::string extract_error_code_from_json_payload(const std::string& payload) {
    Json parsed;
    if (const auto ec = glz::read_json(parsed, payload); ec || !parsed.is_object()) {
        return {};
    }
    const auto& object = parsed.get_object();

    const auto error_it = object.find("error");
    if (error_it != object.end() && error_it->second.is_object()) {
        const auto code = extract_code_or_type(error_it->second.get_object());
        if (!code.empty()) {
            return code;
        }
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        const auto& response = response_it->second.get_object();
        const auto response_error_it = response.find("error");
        if (response_error_it != response.end() && response_error_it->second.is_object()) {
            const auto code = extract_code_or_type(response_error_it->second.get_object());
            if (!code.empty()) {
                return code;
            }
        }
    }
    return {};
}

std::string extract_error_code_from_events(const std::vector<std::string>& events) {
    for (const auto& event : events) {
        const auto code = extract_error_code_from_json_payload(event);
        if (!code.empty()) {
            return code;
        }
    }
    return {};
}

std::string websocket_authority_header(const ParsedUpstreamUrl& parsed) {
    const bool is_ipv6 = parsed.host.find(':') != std::string::npos;
    if (is_ipv6 && (parsed.host.empty() || parsed.host.front() != '[')) {
        return "[" + parsed.host + "]:" + parsed.port;
    }
    return parsed.host + ":" + parsed.port;
}

template <typename WebSocketStream>
void configure_websocket_client(WebSocketStream& ws, const long connect_timeout_ms, const long request_timeout_ms) {
    websocket::stream_base::timeout timeout_cfg{
        .handshake_timeout = std::chrono::milliseconds(connect_timeout_ms),
        .idle_timeout = std::chrono::milliseconds(request_timeout_ms),
        .keep_alive_pings = true,
    };
    ws.set_option(timeout_cfg);
}

template <typename WebSocketStream>
void configure_persistent_bridge_websocket_client(
    WebSocketStream& ws,
    const long connect_timeout_ms,
    const long read_poll_timeout_ms
) {
    websocket::stream_base::timeout timeout_cfg{
        .handshake_timeout = std::chrono::milliseconds(connect_timeout_ms),
        .idle_timeout = std::chrono::milliseconds(read_poll_timeout_ms),
        .keep_alive_pings = false,
    };
    ws.set_option(timeout_cfg);
}

UpstreamExecutionResult websocket_result_with_error(const boost::system::error_code& ec) {
    UpstreamExecutionResult result;
    result.status = 502;
    result.close_code = 1011;
    result.accepted = false;
    result.error_code = is_websocket_timeout(ec) ? "upstream_request_timeout" : "upstream_unavailable";
    result.body = ec.message();
    return result;
}

UpstreamExecutionResult websocket_result_with_init_failure(const std::string& message) {
    UpstreamExecutionResult result;
    result.status = 502;
    result.close_code = 1011;
    result.accepted = false;
    result.error_code = "upstream_transport_init_failed";
    result.body = message;
    return result;
}

UpstreamExecutionResult websocket_result_with_handshake_failure(
    const boost::system::error_code& ec,
    const websocket::response_type& response
) {
    const auto status = response.result_int();
    if (status <= 0) {
        return websocket_result_with_error(ec);
    }

    UpstreamExecutionResult result;
    result.status = static_cast<int>(status);
    result.close_code = 1011;
    result.accepted = false;
    result.headers = capture_websocket_handshake_headers(response);
    result.body = response.body();

    if (!result.body.empty()) {
        result.error_code = internal::normalize_upstream_error_code(extract_error_code_from_json_payload(result.body));
    }
    if (result.error_code.empty() || result.error_code == "upstream_error") {
        if (result.status == 401) {
            result.error_code = "invalid_api_key";
        } else if (result.status >= 500) {
            result.error_code = "upstream_unavailable";
        } else {
            result.error_code = "upstream_error";
        }
    }
    return result;
}

std::int64_t monotonic_now_ms() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count()
    );
}

std::string find_header_case_insensitive(const openai::HeaderMap& headers, const std::string_view key) {
    for (const auto& [name, value] : headers) {
        if (core::text::equals_case_insensitive(name, key)) {
            return std::string(core::text::trim_ascii(value));
        }
    }
    return {};
}

std::optional<std::string> continuity_key_from_headers(const openai::HeaderMap& headers) {
    const auto codex_session_id = find_header_case_insensitive(headers, "x-codex-session-id");
    const auto client_session_id = find_header_case_insensitive(headers, "session_id");
    const auto turn_state = find_header_case_insensitive(headers, "x-codex-turn-state");

    // Build a composite continuity key from all available scope identifiers.
    // This prevents unrelated sessions from sharing one persistent upstream bridge
    // when any single identifier is reused or absent.
    std::string key;
    if (!codex_session_id.empty()) {
        key += "codex:";
        key += codex_session_id;
    }
    if (!client_session_id.empty()) {
        if (!key.empty()) {
            key += "|";
        }
        key += "client:";
        key += client_session_id;
    }
    if (!turn_state.empty()) {
        if (!key.empty()) {
            key += "|";
        }
        key += "turn:";
        key += turn_state;
    }
    if (key.empty()) {
        return std::nullopt;
    }
    return key;
}

std::optional<std::string> parse_previous_response_id_from_payload(const std::string& payload_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, payload_body); ec || !payload.is_object()) {
        return std::nullopt;
    }

    const auto find_previous_response = [](const Json::object_t& object) -> std::optional<std::string> {
        const auto it = object.find("previous_response_id");
        if (it == object.end() || !it->second.is_string()) {
            return std::nullopt;
        }
        auto value = std::string(core::text::trim_ascii(it->second.get_string()));
        if (value.empty()) {
            return std::nullopt;
        }
        return value;
    };

    if (const auto top_level = find_previous_response(payload.get_object()); top_level.has_value()) {
        return top_level;
    }

    const auto response_it = payload.get_object().find("response");
    if (response_it != payload.get_object().end() && response_it->second.is_object()) {
        return find_previous_response(response_it->second.get_object());
    }
    return std::nullopt;
}

bool is_response_create_payload(const std::string& payload_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, payload_body); ec || !payload.is_object()) {
        return false;
    }

    const auto type_it = payload.get_object().find("type");
    return type_it != payload.get_object().end() && type_it->second.is_string() &&
           type_it->second.get_string() == "response.create";
}

UpstreamExecutionResult no_active_upstream_websocket_session_result() {
    UpstreamExecutionResult result;
    result.status = 400;
    result.close_code = 1000;
    result.accepted = false;
    result.error_code = "invalid_request_error";
    result.body = openai::build_error_envelope(
        "invalid_request_error",
        "WebSocket connection has no active upstream session",
        "invalid_request_error"
    );
    return result;
}

std::string scoped_continuity_key(
    const std::string_view continuity_key,
    const std::string_view api_key_scope,
    const std::string_view session_scope
) {
    std::string scoped;
    if (!api_key_scope.empty()) {
        scoped += "api_key_scope:";
        scoped += std::string(api_key_scope);
        scoped += "|";
    }
    if (!session_scope.empty()) {
        scoped += "session_scope:";
        scoped += std::string(session_scope);
        scoped += "|";
    }
    scoped += std::string(continuity_key);
    return scoped;
}

std::string bridge_key_fingerprint(const std::string_view value) {
    const auto hash = std::hash<std::string_view>{}(value);
    std::ostringstream stream;
    stream << std::hex << hash;
    return stream.str();
}

struct BridgeConnection {
    bool tls = false;
    long read_poll_timeout_ms = 0;
    std::unique_ptr<asio::io_context> io_context;
    std::unique_ptr<ssl::context> ssl_context;
    std::unique_ptr<websocket::stream<beast::tcp_stream>> plain_ws;
    std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> tls_ws;
    openai::HeaderMap handshake_headers;
};

void close_bridge_connection(BridgeConnection& connection) {
    boost::system::error_code close_ec;
    if (connection.tls) {
        if (connection.tls_ws) {
            // Send RFC 6455 close frame before TCP shutdown.
            connection.tls_ws->close(websocket::close_code::normal, close_ec);
            beast::get_lowest_layer(*connection.tls_ws).socket().shutdown(tcp::socket::shutdown_both, close_ec);
            beast::get_lowest_layer(*connection.tls_ws).socket().close(close_ec);
        }
        return;
    }
    if (connection.plain_ws) {
        // Send RFC 6455 close frame before TCP shutdown.
        connection.plain_ws->close(websocket::close_code::normal, close_ec);
        beast::get_lowest_layer(*connection.plain_ws).socket().shutdown(tcp::socket::shutdown_both, close_ec);
        beast::get_lowest_layer(*connection.plain_ws).socket().close(close_ec);
    }
}

boost::system::error_code
write_bridge_connection(BridgeConnection& connection, const std::string& body, const bool binary_payload) {
    boost::system::error_code ec;
    if (connection.tls) {
        if (connection.tls_ws) {
            connection.tls_ws->binary(binary_payload);
            connection.tls_ws->write(asio::buffer(body), ec);
            return ec;
        }
        return asio::error::operation_aborted;
    }
    if (connection.plain_ws) {
        connection.plain_ws->binary(binary_payload);
        connection.plain_ws->write(asio::buffer(body), ec);
        return ec;
    }
    return asio::error::operation_aborted;
}

boost::system::error_code read_bridge_connection(BridgeConnection& connection, std::string& payload) {
    boost::system::error_code ec;
    beast::flat_buffer read_buffer;
    read_buffer.max_size(kMaxUpstreamFrameBytes);
    if (connection.tls) {
        if (connection.tls_ws) {
            if (connection.read_poll_timeout_ms > 0) {
                beast::get_lowest_layer(*connection.tls_ws)
                    .expires_after(std::chrono::milliseconds(connection.read_poll_timeout_ms));
            }
            connection.tls_ws->read(read_buffer, ec);
        } else {
            ec = asio::error::operation_aborted;
        }
    } else {
        if (connection.plain_ws) {
            if (connection.read_poll_timeout_ms > 0) {
                beast::get_lowest_layer(*connection.plain_ws)
                    .expires_after(std::chrono::milliseconds(connection.read_poll_timeout_ms));
            }
            connection.plain_ws->read(read_buffer, ec);
        } else {
            ec = asio::error::operation_aborted;
        }
    }
    if (!ec) {
        payload = beast::buffers_to_string(read_buffer.data());
    }
    return ec;
}

std::optional<BridgeConnection> open_bridge_connection(
    const ParsedUpstreamUrl& parsed,
    const std::string_view ws_scheme,
    const std::string& target,
    const openai::HeaderMap& headers,
    const long connect_timeout_ms,
    const long request_timeout_ms,
    const long read_poll_timeout_ms,
    UpstreamExecutionResult* failure
) {
    if (failure == nullptr) {
        return std::nullopt;
    }
    (void)request_timeout_ms;

    BridgeConnection connection;
    connection.read_poll_timeout_ms = read_poll_timeout_ms;
    connection.io_context = std::make_unique<asio::io_context>();
    if (!connection.io_context) {
        *failure = websocket_result_with_init_failure("Failed to allocate upstream io_context");
        return std::nullopt;
    }

    tcp::resolver resolver(*connection.io_context);
    boost::system::error_code ec;
    const auto endpoints = resolver.resolve(parsed.host, parsed.port, ec);
    if (ec) {
        *failure = websocket_result_with_error(ec);
        return std::nullopt;
    }

    const auto host_header = websocket_authority_header(parsed);
    auto apply_request_headers = [&](websocket::request_type& request) {
        apply_websocket_request_headers(request, headers);
    };

    if (ws_scheme == "ws") {
        connection.tls = false;
        connection.plain_ws = std::make_unique<websocket::stream<beast::tcp_stream>>(*connection.io_context);
        auto& ws = *connection.plain_ws;
        beast::get_lowest_layer(ws).expires_after(std::chrono::milliseconds(connect_timeout_ms));
        beast::get_lowest_layer(ws).connect(endpoints, ec);
        if (ec) {
            *failure = websocket_result_with_error(ec);
            return std::nullopt;
        }
        configure_persistent_bridge_websocket_client(ws, connect_timeout_ms, read_poll_timeout_ms);
        ws.set_option(websocket::stream_base::decorator(apply_request_headers));
        websocket::response_type handshake_response;
        ws.handshake(handshake_response, host_header, target, ec);
        if (ec) {
            *failure = websocket_result_with_handshake_failure(ec, handshake_response);
            return std::nullopt;
        }
        connection.handshake_headers = capture_websocket_handshake_headers(handshake_response);
        return connection;
    }

    connection.tls = true;
    connection.ssl_context = std::make_unique<ssl::context>(ssl::context::tls_client);
    if (!connection.ssl_context) {
        *failure = websocket_result_with_init_failure("Failed to allocate upstream TLS context");
        return std::nullopt;
    }
    connection.ssl_context->set_default_verify_paths(ec);
    if (ec) {
        *failure = websocket_result_with_init_failure(ec.message());
        return std::nullopt;
    }
    load_system_root_certificates(*connection.ssl_context);

    connection.tls_ws = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(
        *connection.io_context,
        *connection.ssl_context
    );
    auto& ws = *connection.tls_ws;
    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), parsed.host.c_str())) {
        *failure = websocket_result_with_init_failure("Failed to set TLS SNI host");
        return std::nullopt;
    }

    beast::get_lowest_layer(ws).expires_after(std::chrono::milliseconds(connect_timeout_ms));
    beast::get_lowest_layer(ws).connect(endpoints, ec);
    if (ec) {
        *failure = websocket_result_with_error(ec);
        return std::nullopt;
    }

    ws.next_layer().set_verify_mode(ssl::verify_peer);
    ws.next_layer().handshake(ssl::stream_base::client, ec);
    if (ec) {
        *failure = websocket_result_with_error(ec);
        return std::nullopt;
    }

    configure_persistent_bridge_websocket_client(ws, connect_timeout_ms, read_poll_timeout_ms);
    ws.set_option(websocket::stream_base::decorator(apply_request_headers));
    websocket::response_type handshake_response;
    ws.handshake(handshake_response, host_header, target, ec);
    if (ec) {
        *failure = websocket_result_with_handshake_failure(ec, handshake_response);
        return std::nullopt;
    }
    connection.handshake_headers = capture_websocket_handshake_headers(handshake_response);
    return connection;
}

struct PersistentBridgeSession {
    std::mutex mutex;
    BridgeConnection connection;
    std::string account_id;
    std::string traffic_account_id;
    std::atomic<std::int64_t> updated_at_ms{0};
    std::atomic_bool cancel_requested{false};
};

struct PersistentBridgeState {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<PersistentBridgeSession>> sessions;
};

PersistentBridgeState& persistent_bridge_state() {
    static PersistentBridgeState state;
    return state;
}

constexpr std::int64_t kPersistentBridgeTtlMs = 30LL * 60LL * 1000LL;
constexpr long kDefaultPersistentBridgePollTimeoutMs = 1000;
constexpr std::size_t kPersistentBridgeMaxSessions = 256;

long persistent_bridge_poll_timeout_ms(const long request_timeout_ms) {
    const auto configured = env_long_or_default(
        "TIGHTROPE_UPSTREAM_BRIDGE_POLL_TIMEOUT_MS",
        kDefaultPersistentBridgePollTimeoutMs
    );
    const auto minimum = 100L;
    const auto maximum = request_timeout_ms > 0 ? request_timeout_ms : configured;
    return std::clamp(configured, minimum, maximum);
}

void purge_stale_persistent_bridge_sessions_locked(PersistentBridgeState& state, const std::int64_t now_ms) {
    for (auto it = state.sessions.begin(); it != state.sessions.end();) {
        const auto& session = it->second;
        const auto updated_at_ms = session->updated_at_ms.load(std::memory_order_relaxed);
        const auto stale = now_ms >= updated_at_ms && (now_ms - updated_at_ms) >= kPersistentBridgeTtlMs;
        if (!stale) {
            ++it;
            continue;
        }
        std::lock_guard<std::mutex> session_lock(session->mutex);
        close_bridge_connection(session->connection);
        it = state.sessions.erase(it);
    }
}

void trim_persistent_bridge_sessions_locked(PersistentBridgeState& state) {
    while (state.sessions.size() > kPersistentBridgeMaxSessions) {
        auto lru_it = state.sessions.end();
        for (auto it = state.sessions.begin(); it != state.sessions.end(); ++it) {
            if (lru_it == state.sessions.end() ||
                it->second->updated_at_ms.load(std::memory_order_relaxed) <
                    lru_it->second->updated_at_ms.load(std::memory_order_relaxed)) {
                lru_it = it;
            }
        }
        if (lru_it == state.sessions.end()) {
            break;
        }
        std::lock_guard<std::mutex> session_lock(lru_it->second->mutex);
        close_bridge_connection(lru_it->second->connection);
        state.sessions.erase(lru_it);
    }
}

void reset_persistent_bridge_sessions() {
    auto& state = persistent_bridge_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (auto& [_, session] : state.sessions) {
        std::lock_guard<std::mutex> session_lock(session->mutex);
        close_bridge_connection(session->connection);
    }
    state.sessions.clear();
}

UpstreamExecutionResult execute_over_websocket_with_persistent_bridge(
    const openai::UpstreamRequestPlan& plan,
    const ParsedUpstreamUrl& parsed,
    const std::string_view ws_scheme,
    const std::string& target,
    const long connect_timeout_ms,
    const long request_timeout_ms
) {
    const auto continuity_key = continuity_key_from_headers(plan.headers);
    if (!continuity_key.has_value() || continuity_key->empty()) {
        return websocket_result_with_init_failure("Missing websocket bridge continuity key");
    }

    const auto api_key_scope = find_header_case_insensitive(plan.headers, "x-tightrope-api-key-id");
    std::string session_scope;
    const auto downstream_session_id = find_header_case_insensitive(plan.headers, "session_id");
    const auto codex_session_id = find_header_case_insensitive(plan.headers, "x-codex-session-id");
    if (!downstream_session_id.empty()) {
        session_scope += "client:";
        session_scope += downstream_session_id;
    }
    if (!codex_session_id.empty()) {
        if (!session_scope.empty()) {
            session_scope += "|";
        }
        session_scope += "codex:";
        session_scope += codex_session_id;
    }
    const auto bridge_key = scoped_continuity_key(*continuity_key, api_key_scope, session_scope);
    const auto read_poll_timeout_ms = persistent_bridge_poll_timeout_ms(request_timeout_ms);
    const auto account_id = find_header_case_insensitive(plan.headers, "chatgpt-account-id");
    const auto traffic_account_id = std::string(current_account_traffic_context_account_id());
    const auto previous_response_id = parse_previous_response_id_from_payload(plan.body);
    const bool has_previous_response = previous_response_id.has_value();
    const bool response_create_request = is_response_create_payload(plan.body);
    const bool can_retry_on_fresh_bridge = response_create_request && !has_previous_response;
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "responses_ws_bridge_session_scope",
        "key_fp=" + bridge_key_fingerprint(bridge_key) + " continuity_bytes=" +
            std::to_string(continuity_key->size()) + " session_scope_bytes=" +
            std::to_string(session_scope.size()) + " has_previous_response=" +
            std::string(has_previous_response ? "true" : "false")
    );

    auto& bridge = persistent_bridge_state();
    std::optional<UpstreamExecutionResult> session_connect_failure;
    auto acquire_or_create_session = [&]() -> std::shared_ptr<PersistentBridgeSession> {
        for (;;) {
            std::shared_ptr<PersistentBridgeSession> existing;
            std::shared_ptr<PersistentBridgeSession> displaced;
            bool should_create = false;

            {
                std::lock_guard<std::mutex> lock(bridge.mutex);
                const auto now = monotonic_now_ms();
                purge_stale_persistent_bridge_sessions_locked(bridge, now);
                trim_persistent_bridge_sessions_locked(bridge);

                const auto it = bridge.sessions.find(bridge_key);
                if (it != bridge.sessions.end()) {
                    existing = it->second;
                    const auto cancelled = existing->cancel_requested.load(std::memory_order_relaxed);
                    const auto mismatched_account =
                        !has_previous_response && !account_id.empty() && !existing->account_id.empty() &&
                        existing->account_id != account_id;
                    if (!cancelled && !mismatched_account) {
                        return existing;
                    }
                    bridge.sessions.erase(it);
                    displaced = existing;
                    should_create = true;
                } else {
                    should_create = true;
                }
            }

            if (displaced) {
                std::lock_guard<std::mutex> session_lock(displaced->mutex);
                close_bridge_connection(displaced->connection);
            }

            if (!should_create) {
                continue;
            }
            if (!response_create_request) {
                return {};
            }

            UpstreamExecutionResult connect_failure;
            auto connection = open_bridge_connection(
                parsed,
                ws_scheme,
                target,
                plan.headers,
                connect_timeout_ms,
                request_timeout_ms,
                read_poll_timeout_ms,
                &connect_failure
            );
            if (!connection.has_value()) {
                session_connect_failure = std::move(connect_failure);
                return {};
            }

            auto candidate = std::make_shared<PersistentBridgeSession>();
            candidate->connection = std::move(*connection);
            candidate->account_id = account_id;
            candidate->traffic_account_id = traffic_account_id;
            candidate->updated_at_ms.store(monotonic_now_ms(), std::memory_order_relaxed);

            {
                std::lock_guard<std::mutex> lock(bridge.mutex);
                const auto now = monotonic_now_ms();
                purge_stale_persistent_bridge_sessions_locked(bridge, now);
                trim_persistent_bridge_sessions_locked(bridge);
                auto [inserted_it, inserted] = bridge.sessions.try_emplace(bridge_key, candidate);
                if (inserted) {
                    trim_persistent_bridge_sessions_locked(bridge);
                    return candidate;
                }
                existing = inserted_it->second;
            }

            {
                std::lock_guard<std::mutex> candidate_lock(candidate->mutex);
                close_bridge_connection(candidate->connection);
            }
            if (existing) {
                return existing;
            }
        }
    };

    const auto session = acquire_or_create_session();
    if (!session) {
        if (session_connect_failure.has_value()) {
            return *session_connect_failure;
        }
        if (!response_create_request) {
            return no_active_upstream_websocket_session_result();
        }
        return websocket_result_with_init_failure("Failed to acquire websocket bridge session");
    }

    const long effective_request_timeout_ms = request_timeout_ms > 0 ? request_timeout_ms : kDefaultRequestTimeoutMs;
    auto run_session = [&](const std::shared_ptr<PersistentBridgeSession>& active_session)
        -> std::pair<UpstreamExecutionResult, bool> {
        UpstreamExecutionResult result;
        bool remove_session = false;
        bool transport_error = false;
        bool cancelled_by_downstream_close = false;
        {
            std::unique_lock<std::mutex> session_lock(active_session->mutex);
            if (session_lock.owns_lock()) {
                if (!traffic_account_id.empty() && active_session->traffic_account_id != traffic_account_id) {
                    active_session->traffic_account_id = traffic_account_id;
                }
                const auto effective_traffic_account_id =
                    !traffic_account_id.empty() ? traffic_account_id : active_session->traffic_account_id;
                if (!effective_traffic_account_id.empty()) {
                    record_account_upstream_egress(effective_traffic_account_id, plan.body.size());
                }
                auto write_ec = write_bridge_connection(active_session->connection, plan.body, plan.websocket_binary_payload);
                if (write_ec) {
                    close_bridge_connection(active_session->connection);
                    remove_session = true;
                    transport_error = true;
                    result = websocket_result_with_error(write_ec);
                } else {
                    result.status = 101;
                    result.close_code = 1000;
                    result.accepted = true;
                    result.headers = active_session->connection.handshake_headers;

                    bool saw_terminal_event = false;
                    std::size_t total_upstream_bytes = 0;
                    auto last_activity_ms = monotonic_now_ms();
                    for (;;) {
                        if (active_session->cancel_requested.load(std::memory_order_relaxed)) {
                            close_bridge_connection(active_session->connection);
                            remove_session = true;
                            cancelled_by_downstream_close = true;
                            result.error_code = "stream_incomplete";
                            break;
                        }
                        std::string payload;
                        auto read_ec = read_bridge_connection(active_session->connection, payload);
                        if (is_websocket_timeout(read_ec)) {
                            const auto now_ms = monotonic_now_ms();
                            if (effective_request_timeout_ms > 0 &&
                                (now_ms - last_activity_ms) >= effective_request_timeout_ms) {
                                close_bridge_connection(active_session->connection);
                                remove_session = true;
                                transport_error = true;
                                core::logging::log_event(
                                    core::logging::LogLevel::Warning,
                                    "runtime",
                                    "proxy",
                                    "responses_ws_bridge_session_idle_timeout",
                                    "key_fp=" + bridge_key_fingerprint(bridge_key) + " idle_ms=" +
                                        std::to_string(now_ms - last_activity_ms)
                                );
                                result = websocket_result_with_error(asio::error::timed_out);
                                break;
                            }
                            continue;
                        }
                        if (read_ec == websocket::error::closed) {
                            close_bridge_connection(active_session->connection);
                            remove_session = true;
                            transport_error = true;
                            result.error_code = "stream_incomplete";
                            break;
                        }
                        if (read_ec) {
                            close_bridge_connection(active_session->connection);
                            remove_session = true;
                            transport_error = true;
                            result = websocket_result_with_error(read_ec);
                            break;
                        }

                        if (payload.empty()) {
                            continue;
                        }
                        last_activity_ms = monotonic_now_ms();
                        total_upstream_bytes += payload.size();
                        if (total_upstream_bytes > kMaxUpstreamResponseBytes) {
                            close_bridge_connection(active_session->connection);
                            remove_session = true;
                            result.status = 502;
                            result.close_code = 1011;
                            result.accepted = false;
                            result.error_code = "upstream_response_too_large";
                            break;
                        }
                        if (!effective_traffic_account_id.empty()) {
                            record_account_upstream_ingress(effective_traffic_account_id, payload.size());
                        }
                        result.events.push_back(payload);
                        if (websocket_stream_event_terminal(payload)) {
                            saw_terminal_event = true;
                            break;
                        }
                    }

                    if (!saw_terminal_event && result.error_code.empty()) {
                        result.error_code = "stream_incomplete";
                    }

                    if (!remove_session) {
                        const auto resolved_error = internal::resolved_upstream_error_code(result);
                        if (resolved_error == "previous_response_not_found" || resolved_error == "stream_incomplete") {
                            close_bridge_connection(active_session->connection);
                            remove_session = true;
                        } else {
                            active_session->updated_at_ms.store(monotonic_now_ms(), std::memory_order_relaxed);
                        }
                    }
                }
            }
        }

        if (remove_session) {
            std::lock_guard<std::mutex> lock(bridge.mutex);
            const auto it = bridge.sessions.find(bridge_key);
            if (it != bridge.sessions.end() && it->second == active_session) {
                bridge.sessions.erase(it);
            }
        }

        bool retryable = false;
        if (can_retry_on_fresh_bridge && !cancelled_by_downstream_close) {
            const auto resolved_error = internal::resolved_upstream_error_code(result);
            retryable = transport_error || resolved_error == "stream_incomplete" || resolved_error == "upstream_request_timeout";
        }
        return {std::move(result), retryable};
    };

    auto [first_result, should_retry] = run_session(session);
    if (!should_retry) {
        return first_result;
    }

    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_ws_bridge_session_retry_fresh",
        "key_fp=" + bridge_key_fingerprint(bridge_key)
    );

    {
        std::lock_guard<std::mutex> lock(bridge.mutex);
        const auto it = bridge.sessions.find(bridge_key);
        if (it != bridge.sessions.end() && it->second == session) {
            bridge.sessions.erase(it);
        }
    }
    {
        std::lock_guard<std::mutex> session_lock(session->mutex);
        close_bridge_connection(session->connection);
    }

    session_connect_failure.reset();
    const auto retry_session = acquire_or_create_session();
    if (!retry_session) {
        if (session_connect_failure.has_value()) {
            return *session_connect_failure;
        }
        return first_result;
    }

    auto [retry_result, unused_retry_flag] = run_session(retry_session);
    (void)unused_retry_flag;
    return retry_result;
}

template <typename WebSocketStream>
UpstreamExecutionResult read_websocket_stream(
    WebSocketStream& ws,
    const openai::UpstreamRequestPlan& plan,
    const openai::HeaderMap& handshake_headers,
    const std::string_view traffic_account_id
) {
    boost::system::error_code ec;
    if (!traffic_account_id.empty()) {
        record_account_upstream_egress(traffic_account_id, plan.body.size());
    }
    ws.binary(plan.websocket_binary_payload);
    ws.write(asio::buffer(plan.body), ec);
    if (ec) {
        return websocket_result_with_error(ec);
    }

    UpstreamExecutionResult result;
    result.status = 101;
    result.close_code = 1000;
    result.accepted = true;
    result.headers = handshake_headers;

    bool saw_terminal_event = false;
    beast::flat_buffer read_buffer;
    read_buffer.max_size(kMaxUpstreamFrameBytes);
    std::size_t total_upstream_bytes = 0;
    for (;;) {
        read_buffer.clear();
        ws.read(read_buffer, ec);
        if (ec == websocket::error::closed) {
            ec = {};
            break;
        }
        if (ec) {
            return websocket_result_with_error(ec);
        }

        const auto payload = beast::buffers_to_string(read_buffer.data());
        if (payload.empty()) {
            continue;
        }
        total_upstream_bytes += payload.size();
        if (total_upstream_bytes > kMaxUpstreamResponseBytes) {
            result.error_code = "upstream_response_too_large";
            return result;
        }
        if (!traffic_account_id.empty()) {
            record_account_upstream_ingress(traffic_account_id, payload.size());
        }
        result.events.push_back(payload);
        if (websocket_stream_event_terminal(payload)) {
            saw_terminal_event = true;
            break;
        }
    }

    if (!saw_terminal_event) {
        result.error_code = "stream_incomplete";
    }

    boost::system::error_code close_ec;
    ws.close(websocket::close_code::normal, close_ec);
    return result;
}

UpstreamExecutionResult execute_over_websocket(const openai::UpstreamRequestPlan& plan) {
    UpstreamExecutionResult result;
    result.status = 502;
    result.close_code = 1011;
    result.accepted = false;

    const auto base_url = trim_trailing_slashes(env_or_default("TIGHTROPE_UPSTREAM_BASE_URL", kDefaultUpstreamBaseUrl));
    const auto parsed = parse_upstream_url(base_url);
    if (!parsed.has_value()) {
        result.error_code = "upstream_transport_init_failed";
        result.body = "Invalid upstream URL";
        return result;
    }

    std::string ws_scheme = parsed->scheme;
    if (ws_scheme == "https") {
        ws_scheme = "wss";
    } else if (ws_scheme == "http") {
        ws_scheme = "ws";
    }
    if (ws_scheme != "ws" && ws_scheme != "wss") {
        result.error_code = "upstream_transport_init_failed";
        result.body = "Unsupported websocket upstream scheme";
        return result;
    }

    const auto target = join_upstream_path(parsed->base_path, plan.path);
    const auto connect_timeout_ms = env_long_or_default("TIGHTROPE_UPSTREAM_CONNECT_TIMEOUT_MS", kDefaultConnectTimeoutMs);
    const auto request_timeout_ms = env_long_or_default("TIGHTROPE_UPSTREAM_TIMEOUT_MS", kDefaultRequestTimeoutMs);
    const auto host_header = websocket_authority_header(*parsed);

    if (plan.preserve_upstream_websocket_session) {
        if (const auto continuity_key = continuity_key_from_headers(plan.headers);
            continuity_key.has_value() && !continuity_key->empty()) {
            return execute_over_websocket_with_persistent_bridge(
                plan,
                *parsed,
                ws_scheme,
                target,
                connect_timeout_ms,
                request_timeout_ms
            );
        }
    }

    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    boost::system::error_code ec;
    const auto endpoints = resolver.resolve(parsed->host, parsed->port, ec);
    if (ec) {
        return websocket_result_with_error(ec);
    }

    auto apply_request_headers = [&](websocket::request_type& request) {
        apply_websocket_request_headers(request, plan.headers);
    };

    if (ws_scheme == "ws") {
        websocket::stream<beast::tcp_stream> ws(io_context);
        beast::get_lowest_layer(ws).expires_after(std::chrono::milliseconds(connect_timeout_ms));
        beast::get_lowest_layer(ws).connect(endpoints, ec);
        if (ec) {
            return websocket_result_with_error(ec);
        }
        configure_websocket_client(ws, connect_timeout_ms, request_timeout_ms);
        ws.set_option(websocket::stream_base::decorator(apply_request_headers));
        websocket::response_type handshake_response;
        ws.handshake(handshake_response, host_header, target, ec);
        if (ec) {
            return websocket_result_with_handshake_failure(ec, handshake_response);
        }
        return read_websocket_stream(
            ws,
            plan,
            capture_websocket_handshake_headers(handshake_response),
            current_account_traffic_context_account_id()
        );
    }

    ssl::context ssl_context(ssl::context::tls_client);
    ssl_context.set_default_verify_paths(ec);
    if (ec) {
        result.error_code = "upstream_transport_init_failed";
        result.body = ec.message();
        return result;
    }
    load_system_root_certificates(ssl_context);

    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(io_context, ssl_context);
    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), parsed->host.c_str())) {
        result.error_code = "upstream_transport_init_failed";
        result.body = "Failed to set TLS SNI host";
        return result;
    }

    beast::get_lowest_layer(ws).expires_after(std::chrono::milliseconds(connect_timeout_ms));
    beast::get_lowest_layer(ws).connect(endpoints, ec);
    if (ec) {
        return websocket_result_with_error(ec);
    }

    ws.next_layer().set_verify_mode(ssl::verify_peer);
    ws.next_layer().handshake(ssl::stream_base::client, ec);
    if (ec) {
        return websocket_result_with_error(ec);
    }

    configure_websocket_client(ws, connect_timeout_ms, request_timeout_ms);
    ws.set_option(websocket::stream_base::decorator(apply_request_headers));
    websocket::response_type handshake_response;
    ws.handshake(handshake_response, host_header, target, ec);
    if (ec) {
        return websocket_result_with_handshake_failure(ec, handshake_response);
    }
    return read_websocket_stream(
        ws,
        plan,
        capture_websocket_handshake_headers(handshake_response),
        current_account_traffic_context_account_id()
    );
}

UpstreamExecutionResult execute_over_http(
    const openai::UpstreamRequestPlan& plan,
    const bool websocket_fallback
) {
    static std::once_flag curl_once;
    std::call_once(curl_once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            fprintf(stderr, "[tightrope] curl_global_init failed; upstream HTTP transport may be unavailable\n");
        }
    });

    UpstreamExecutionResult result;
    result.status = 502;
    result.close_code = websocket_fallback ? 1011 : 1000;
    result.accepted = false;

    auto curl_guard = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(
        curl_easy_init(), curl_easy_cleanup);
    if (!curl_guard) {
        result.error_code = "upstream_transport_init_failed";
        return result;
    }
    CURL* curl = curl_guard.get();

    std::string response_body;
    std::string request_body = websocket_fallback ? normalize_ws_payload_for_http_fallback(plan.body) : plan.body;
    const auto url = build_upstream_url(plan.path);
    openai::HeaderMap response_headers;
    HttpWriteContext write_context;
    write_context.output = &response_body;
    write_context.traffic_account_id = std::string(current_account_traffic_context_account_id());

    const bool request_compression_enabled = should_apply_request_compression(plan, websocket_fallback);
    bool request_body_compressed = false;
    if (request_compression_enabled) {
        if (const auto compressed = compress_request_body_zstd(request_body); compressed.has_value()) {
            request_body = *compressed;
            request_body_compressed = true;
        } else {
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "proxy",
                "upstream_request_compression_skipped",
                "path=" + plan.path + " reason=zstd_unavailable_or_failed"
            );
        }
    }

    curl_slist* headers = nullptr;
    // RAII guard: frees headers on any exit path (including exceptions).
    // Holds a reference so it always frees whatever headers points to at destruction.
    struct SlistGuard {
        curl_slist*& ptr;
        ~SlistGuard() { curl_slist_free_all(ptr); }
    } headers_guard{headers};
    bool has_accept = false;
    bool has_content_type = false;
    bool has_content_encoding = false;
    for (const auto& [key, value] : plan.headers) {
        const auto lowered = lower_ascii(key);
        if (lowered == "content-length") {
            continue;
        }
        if (websocket_fallback && lowered == "accept") {
            headers = append_header(headers, "Accept", "text/event-stream");
            has_accept = true;
            continue;
        }
        if (websocket_fallback && lowered == "content-type") {
            headers = append_header(headers, "Content-Type", "application/json");
            has_content_type = true;
            continue;
        }
        headers = append_header(headers, key, value);
        if (lowered == "accept") {
            has_accept = true;
        }
        if (lowered == "content-type") {
            has_content_type = true;
        }
        if (lowered == "content-encoding") {
            has_content_encoding = true;
        }
    }
    if ((plan.transport == "http-sse" || websocket_fallback) && !has_accept) {
        headers = append_header(headers, "Accept", "text/event-stream");
    }
    if (websocket_fallback && !has_content_type) {
        headers = append_header(headers, "Content-Type", "application/json");
    }
    if (request_body_compressed && !has_content_encoding) {
        headers = append_header(headers, "Content-Encoding", "zstd");
    }

    const auto connect_timeout_ms = env_long_or_default("TIGHTROPE_UPSTREAM_CONNECT_TIMEOUT_MS", kDefaultConnectTimeoutMs);
    const auto request_timeout_ms = env_long_or_default("TIGHTROPE_UPSTREAM_TIMEOUT_MS", kDefaultRequestTimeoutMs);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent.data());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE,
                     static_cast<curl_off_t>(kMaxUpstreamResponseBytes));
    if (!write_context.traffic_account_id.empty()) {
        record_account_upstream_egress(write_context.traffic_account_id, request_body.size());
    }

    const auto curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK) {
        if (write_context.overflowed || curl_result == CURLE_WRITE_ERROR) {
            result.error_code = "upstream_response_too_large";
        } else {
            result.error_code =
                curl_result == CURLE_OPERATION_TIMEDOUT ? "upstream_request_timeout" : "upstream_unavailable";
        }
        result.body = std::string(curl_easy_strerror(curl_result));
        return result;
    }

    long status_code = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    result.status = static_cast<int>(status_code);
    result.headers = std::move(response_headers);

    const bool expects_sse = plan.transport == "http-sse" || websocket_fallback;
    if (expects_sse) {
        result.events = parse_sse_events(response_body);
        if (result.events.empty()) {
            result.body = response_body;
        }
    } else {
        result.body = response_body;
    }

    if (result.status >= 400) {
        const auto extracted_code =
            !result.body.empty() ? extract_error_code_from_json_payload(result.body) : extract_error_code_from_events(result.events);
        result.error_code = internal::normalize_upstream_error_code(extracted_code);
        result.accepted = false;
        result.close_code = websocket_fallback ? 1011 : 1000;
        return result;
    }

    if (websocket_fallback) {
        result.accepted = !result.events.empty();
        result.close_code = result.accepted ? 1000 : 1011;
        if (!result.accepted && result.error_code.empty()) {
            result.error_code = "stream_incomplete";
        }
    }

    return result;
}

class CurlUpstreamTransport final : public UpstreamTransport {
public:
    UpstreamExecutionResult execute(const openai::UpstreamRequestPlan& plan) override {
        if (plan.transport == "websocket") {
            return execute_over_websocket(plan);
        }
        return execute_over_http(plan, /*websocket_fallback=*/false);
    }
};

std::mutex& transport_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<UpstreamTransport>& transport_instance() {
    static std::shared_ptr<UpstreamTransport> instance = std::make_shared<CurlUpstreamTransport>();
    return instance;
}

} // namespace

void set_upstream_transport(const std::shared_ptr<UpstreamTransport> transport) {
    std::lock_guard lock(transport_mutex());
    reset_persistent_bridge_sessions();
    transport_instance() = transport ? transport : std::make_shared<CurlUpstreamTransport>();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "proxy", "upstream_transport_set");
}

void reset_upstream_transport() {
    std::lock_guard lock(transport_mutex());
    reset_persistent_bridge_sessions();
    transport_instance() = std::make_shared<CurlUpstreamTransport>();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "proxy", "upstream_transport_reset");
}

UpstreamExecutionResult execute_upstream_plan(const openai::UpstreamRequestPlan& plan) {
    std::shared_ptr<UpstreamTransport> transport;
    {
        std::lock_guard lock(transport_mutex());
        transport = transport_instance();
    }
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "upstream_execute_start",
        internal::build_upstream_plan_detail(plan)
    );
    const auto result = transport->execute(plan);
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "upstream_execute_complete",
        internal::build_upstream_result_detail(result)
    );
    return result;
}

void request_upstream_websocket_bridge_cancel(const openai::HeaderMap& headers) {
    const auto continuity_key = continuity_key_from_headers(headers);
    if (!continuity_key.has_value() || continuity_key->empty()) {
        return;
    }

    const auto api_key_scope = find_header_case_insensitive(headers, "x-tightrope-api-key-id");
    std::string session_scope;
    const auto downstream_session_id = find_header_case_insensitive(headers, "session_id");
    const auto codex_session_id = find_header_case_insensitive(headers, "x-codex-session-id");
    if (!downstream_session_id.empty()) {
        session_scope += "client:";
        session_scope += downstream_session_id;
    }
    if (!codex_session_id.empty()) {
        if (!session_scope.empty()) {
            session_scope += "|";
        }
        session_scope += "codex:";
        session_scope += codex_session_id;
    }
    const auto bridge_key = scoped_continuity_key(*continuity_key, api_key_scope, session_scope);

    auto& bridge = persistent_bridge_state();
    {
        std::lock_guard<std::mutex> lock(bridge.mutex);
        const auto it = bridge.sessions.find(bridge_key);
        if (it == bridge.sessions.end()) {
            return;
        }
        it->second->cancel_requested.store(true, std::memory_order_relaxed);
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_ws_bridge_session_cancel_requested",
        "key_fp=" + bridge_key_fingerprint(bridge_key)
    );
}

} // namespace tightrope::proxy
