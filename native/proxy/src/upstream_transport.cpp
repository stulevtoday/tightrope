#include "upstream_transport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <memory>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <curl/curl.h>
#include <glaze/glaze.hpp>
#include <openssl/ssl.h>

#include "internal/proxy_error_policy.h"
#include "internal/proxy_service_helpers.h"
#include "logging/logger.h"
#include "text/ascii.h"

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

size_t write_callback(char* ptr, const size_t size, const size_t nmemb, void* userdata) {
    if (ptr == nullptr || userdata == nullptr) {
        return 0;
    }
    auto* output = static_cast<std::string*>(userdata);
    output->append(ptr, size * nmemb);
    return size * nmemb;
}

curl_slist* append_header(curl_slist* headers, const std::string_view key, const std::string_view value) {
    const auto line = std::string(key) + ": " + std::string(value);
    return curl_slist_append(headers, line.c_str());
}

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

UpstreamExecutionResult websocket_result_with_error(const boost::system::error_code& ec) {
    UpstreamExecutionResult result;
    result.status = 502;
    result.close_code = 1011;
    result.accepted = false;
    result.error_code = is_websocket_timeout(ec) ? "upstream_request_timeout" : "upstream_unavailable";
    result.body = ec.message();
    return result;
}

template <typename WebSocketStream>
UpstreamExecutionResult read_websocket_stream(WebSocketStream& ws, const openai::UpstreamRequestPlan& plan) {
    boost::system::error_code ec;
    ws.text(true);
    ws.write(asio::buffer(plan.body), ec);
    if (ec) {
        return websocket_result_with_error(ec);
    }

    UpstreamExecutionResult result;
    result.status = 101;
    result.close_code = 1000;
    result.accepted = true;

    bool saw_terminal_event = false;
    beast::flat_buffer read_buffer;
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
        ws.handshake(host_header, target, ec);
        if (ec) {
            return websocket_result_with_error(ec);
        }
        return read_websocket_stream(ws, plan);
    }

    ssl::context ssl_context(ssl::context::tls_client);
    ssl_context.set_default_verify_paths(ec);
    if (ec) {
        result.error_code = "upstream_transport_init_failed";
        result.body = ec.message();
        return result;
    }

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
    ws.handshake(host_header, target, ec);
    if (ec) {
        return websocket_result_with_error(ec);
    }
    return read_websocket_stream(ws, plan);
}

UpstreamExecutionResult execute_over_http(
    const openai::UpstreamRequestPlan& plan,
    const bool websocket_fallback
) {
    static std::once_flag curl_once;
    std::call_once(curl_once, [] { (void)curl_global_init(CURL_GLOBAL_DEFAULT); });

    UpstreamExecutionResult result;
    result.status = 502;
    result.close_code = websocket_fallback ? 1011 : 1000;
    result.accepted = false;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error_code = "upstream_transport_init_failed";
        return result;
    }

    std::string response_body;
    std::string request_body = websocket_fallback ? normalize_ws_payload_for_http_fallback(plan.body) : plan.body;
    const auto url = build_upstream_url(plan.path);

    curl_slist* headers = nullptr;
    bool has_accept = false;
    bool has_content_type = false;
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
    }
    if ((plan.transport == "http-sse" || websocket_fallback) && !has_accept) {
        headers = append_header(headers, "Accept", "text/event-stream");
    }
    if (websocket_fallback && !has_content_type) {
        headers = append_header(headers, "Content-Type", "application/json");
    }

    const auto connect_timeout_ms = env_long_or_default("TIGHTROPE_UPSTREAM_CONNECT_TIMEOUT_MS", kDefaultConnectTimeoutMs);
    const auto request_timeout_ms = env_long_or_default("TIGHTROPE_UPSTREAM_TIMEOUT_MS", kDefaultRequestTimeoutMs);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent.data());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const auto curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK) {
        result.error_code =
            curl_result == CURLE_OPERATION_TIMEDOUT ? "upstream_request_timeout" : "upstream_unavailable";
        result.body = std::string(curl_easy_strerror(curl_result));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }

    long status_code = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    result.status = static_cast<int>(status_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

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
            auto websocket_result = execute_over_websocket(plan);
            if (websocket_result.status == 101 && websocket_result.accepted && !websocket_result.events.empty()) {
                return websocket_result;
            }
            if (websocket_result.status == 101 && websocket_result.accepted && websocket_result.error_code == "stream_incomplete") {
                return websocket_result;
            }
            if (websocket_result.status == 101 && websocket_result.accepted) {
                return websocket_result;
            }
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "proxy",
                "upstream_websocket_fallback_http",
                "status=" + std::to_string(websocket_result.status) +
                    " code=" + (websocket_result.error_code.empty() ? std::string("none") : websocket_result.error_code) +
                    " message=" + (websocket_result.body.empty() ? std::string("none") : websocket_result.body)
            );
            return execute_over_http(plan, /*websocket_fallback=*/true);
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
    transport_instance() = transport ? transport : std::make_shared<CurlUpstreamTransport>();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "proxy", "upstream_transport_set");
}

void reset_upstream_transport() {
    std::lock_guard lock(transport_mutex());
    transport_instance() = std::make_shared<CurlUpstreamTransport>();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "proxy", "upstream_transport_reset");
}

UpstreamExecutionResult execute_upstream_plan(const openai::UpstreamRequestPlan& plan) {
    std::lock_guard lock(transport_mutex());
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "upstream_execute_start",
        internal::build_upstream_plan_detail(plan)
    );
    const auto result = transport_instance()->execute(plan);
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "upstream_execute_complete",
        internal::build_upstream_result_detail(result)
    );
    return result;
}

} // namespace tightrope::proxy
