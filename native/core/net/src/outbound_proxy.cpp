#include "net/outbound_proxy.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "net/host_port.h"
#include "text/ascii.h"

namespace tightrope::core::net {

namespace {

constexpr const char* kOutboundProxyEnv = "TIGHTROPE_OUTBOUND_PROXY_URL";

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

void clear_error(std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string normalized_url_for_endpoint(const std::string_view scheme, const HostPortEndpoint& endpoint) {
    return std::string(scheme) + "://" + host_port_to_string(endpoint);
}

std::optional<HostPortEndpoint> parse_proxy_authority(
    const std::string_view authority,
    std::string* error
) {
    if (authority.empty()) {
        set_error(error, "proxy URL is missing host:port");
        return std::nullopt;
    }
    if (authority.find('@') != std::string_view::npos) {
        set_error(error, "proxy credentials in URL are not supported; use a local unauthenticated proxy inbound");
        return std::nullopt;
    }

    auto endpoint = parse_host_port(authority);
    if (!endpoint.has_value()) {
        set_error(error, "proxy URL must contain a valid host:port endpoint");
        return std::nullopt;
    }
    return endpoint;
}

} // namespace

std::optional<OutboundProxyConfig> parse_outbound_proxy_url(
    const std::string_view value,
    std::string* error
) {
    clear_error(error);
    const std::string trimmed_storage(core::text::trim_ascii(value));
    const std::string_view trimmed(trimmed_storage);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    std::string scheme = "socks5h";
    std::string_view authority = trimmed;
    const auto scheme_separator = trimmed.find("://");
    if (scheme_separator != std::string_view::npos) {
        scheme = lower_ascii(std::string(trimmed.substr(0, scheme_separator)));
        authority = trimmed.substr(scheme_separator + 3);
    }

    const auto path_start = authority.find_first_of("/?#");
    if (path_start != std::string_view::npos) {
        authority = authority.substr(0, path_start);
    }

    if (scheme != "http" && scheme != "socks5" && scheme != "socks5h") {
        set_error(error, "unsupported proxy scheme; use http://, socks5://, or socks5h://");
        return std::nullopt;
    }

    const auto endpoint = parse_proxy_authority(authority, error);
    if (!endpoint.has_value()) {
        return std::nullopt;
    }

    const bool socks = scheme == "socks5" || scheme == "socks5h";
    return OutboundProxyConfig{
        .protocol = socks ? OutboundProxyProtocol::Socks5 : OutboundProxyProtocol::HttpConnect,
        .url = normalized_url_for_endpoint(scheme, *endpoint),
        .scheme = scheme,
        .host = endpoint->host,
        .port = endpoint->port,
        .remote_dns = scheme == "socks5h",
    };
}

std::optional<OutboundProxyConfig> outbound_proxy_from_env(std::string* error) {
    clear_error(error);
    const auto* raw = std::getenv(kOutboundProxyEnv);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    return parse_outbound_proxy_url(raw, error);
}

bool apply_curl_outbound_proxy(CURL* curl, std::string* error) {
    clear_error(error);
    if (curl == nullptr) {
        set_error(error, "curl handle is null");
        return false;
    }

    auto proxy = outbound_proxy_from_env(error);
    if (!proxy.has_value()) {
        return error == nullptr || error->empty();
    }

    CURLcode code = curl_easy_setopt(curl, CURLOPT_PROXY, proxy->url.c_str());
    if (code != CURLE_OK) {
        set_error(error, std::string("failed to set curl proxy: ") + curl_easy_strerror(code));
        return false;
    }
    code = curl_easy_setopt(curl, CURLOPT_NOPROXY, "");
    if (code != CURLE_OK) {
        set_error(error, std::string("failed to clear curl no-proxy list: ") + curl_easy_strerror(code));
        return false;
    }

    if (proxy->scheme == "http") {
        code = curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
    } else if (proxy->scheme == "socks5h") {
        code = curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
    } else {
        code = curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
    }
    if (code != CURLE_OK) {
        set_error(error, std::string("failed to set curl proxy type: ") + curl_easy_strerror(code));
        return false;
    }
    return true;
}

std::string outbound_proxy_endpoint_label(const OutboundProxyConfig& config) {
    return config.scheme + "://" + host_port_to_string({.host = config.host, .port = config.port});
}

} // namespace tightrope::core::net
