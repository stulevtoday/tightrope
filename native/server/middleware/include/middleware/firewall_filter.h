#pragma once
// firewall_filter middleware

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "net/ip_address.h"
#include "openai/upstream_headers.h"

namespace tightrope::server::middleware {

struct FirewallFilterConfig {
    bool trust_proxy_headers = false;
    std::vector<core::net::IpNetwork> trusted_proxy_networks;
};

struct FirewallFilterDecision {
    bool allow = true;
    int status = 200;
    std::string body;
    std::optional<std::string> client_ip;
};

[[nodiscard]] bool is_firewall_protected_path(std::string_view path) noexcept;
[[nodiscard]] std::vector<core::net::IpNetwork>
parse_trusted_proxy_networks(const std::vector<std::string>& cidrs) noexcept;
[[nodiscard]] std::optional<std::string> resolve_connection_client_ip(
    const proxy::openai::HeaderMap& headers,
    const std::optional<std::string_view>& socket_ip,
    bool trust_proxy_headers,
    const std::vector<core::net::IpNetwork>& trusted_proxy_networks = {}
) noexcept;
[[nodiscard]] FirewallFilterDecision evaluate_firewall_request(
    sqlite3* db,
    std::string_view path,
    const proxy::openai::HeaderMap& headers,
    const std::optional<std::string_view>& socket_ip,
    const FirewallFilterConfig& config = {}
) noexcept;

} // namespace tightrope::server::middleware
