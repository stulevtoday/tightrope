#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <curl/curl.h>

namespace tightrope::core::net {

enum class OutboundProxyProtocol {
    HttpConnect,
    Socks5,
};

struct OutboundProxyConfig {
    OutboundProxyProtocol protocol = OutboundProxyProtocol::Socks5;
    std::string url;
    std::string scheme;
    std::string host;
    std::uint16_t port = 0;
    bool remote_dns = false;
};

[[nodiscard]] std::optional<OutboundProxyConfig> parse_outbound_proxy_url(
    std::string_view value,
    std::string* error = nullptr
);

[[nodiscard]] std::optional<OutboundProxyConfig> outbound_proxy_from_env(std::string* error = nullptr);

[[nodiscard]] bool apply_curl_outbound_proxy(CURL* curl, std::string* error = nullptr);

[[nodiscard]] std::string outbound_proxy_endpoint_label(const OutboundProxyConfig& config);

} // namespace tightrope::core::net
