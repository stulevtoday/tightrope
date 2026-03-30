#include "middleware/firewall_filter.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "text/ascii.h"
#include "repositories/firewall_repo.h"
#include "openai/error_envelope.h"

namespace tightrope::server::middleware {

namespace {

std::optional<std::string_view> header_value_case_insensitive(
    const proxy::openai::HeaderMap& headers,
    std::string_view name
) noexcept {
    for (const auto& [key, value] : headers) {
        if (core::text::equals_case_insensitive(key, name)) {
            return std::string_view(value);
        }
    }
    return std::nullopt;
}

bool is_trusted_proxy_source(
    std::string_view host,
    const std::vector<core::net::IpNetwork>& trusted_proxy_networks
) noexcept {
    if (trusted_proxy_networks.empty()) {
        return false;
    }
    const auto normalized = core::net::normalize_ip_address(host);
    if (!normalized.has_value()) {
        return false;
    }
    for (const auto& network : trusted_proxy_networks) {
        if (core::net::is_ip_in_network(*normalized, network)) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> resolve_client_ip_from_xff_chain(
    std::string_view socket_ip,
    std::string_view forwarded_for,
    const std::vector<core::net::IpNetwork>& trusted_proxy_networks
) noexcept {
    std::vector<std::string> hops;
    std::size_t start = 0;
    while (start <= forwarded_for.size()) {
        const auto comma = forwarded_for.find(',', start);
        const auto chunk = comma == std::string_view::npos ? forwarded_for.substr(start) : forwarded_for.substr(start, comma - start);
        const auto hop = core::text::trim_ascii(chunk);
        if (hop.empty()) {
            return std::nullopt;
        }
        if (!core::net::is_valid_ip_address(hop)) {
            return std::nullopt;
        }
        hops.push_back(std::move(hop));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    if (hops.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> chain = hops;
    chain.emplace_back(socket_ip);

    std::string resolved = std::string(socket_ip);
    for (std::size_t index = chain.size() - 1; index > 0; --index) {
        const auto& current_proxy = chain[index];
        const auto& previous_hop = chain[index - 1];
        if (!is_trusted_proxy_source(current_proxy, trusted_proxy_networks)) {
            resolved = current_proxy;
            break;
        }
        resolved = previous_hop;
    }
    return resolved;
}

} // namespace

bool is_firewall_protected_path(const std::string_view path) noexcept {
    if (path == "/backend-api/codex" || core::text::starts_with(path, "/backend-api/codex/")) {
        return true;
    }
    return path == "/v1" || core::text::starts_with(path, "/v1/");
}

std::vector<core::net::IpNetwork> parse_trusted_proxy_networks(const std::vector<std::string>& cidrs) noexcept {
    std::vector<core::net::IpNetwork> networks;
    networks.reserve(cidrs.size());
    for (const auto& cidr : cidrs) {
        if (const auto network = core::net::parse_ip_network(cidr); network.has_value()) {
            networks.push_back(*network);
        }
    }
    return networks;
}

std::optional<std::string> resolve_connection_client_ip(
    const proxy::openai::HeaderMap& headers,
    const std::optional<std::string_view>& socket_ip,
    const bool trust_proxy_headers,
    const std::vector<core::net::IpNetwork>& trusted_proxy_networks
) noexcept {
    if (!socket_ip.has_value()) {
        return std::nullopt;
    }

    if (trust_proxy_headers && is_trusted_proxy_source(*socket_ip, trusted_proxy_networks)) {
        if (const auto forwarded_for = header_value_case_insensitive(headers, "x-forwarded-for"); forwarded_for.has_value()) {
            if (const auto resolved = resolve_client_ip_from_xff_chain(*socket_ip, *forwarded_for, trusted_proxy_networks);
                resolved.has_value()) {
                return resolved;
            }
        }
    }

    return std::string(*socket_ip);
}

FirewallFilterDecision evaluate_firewall_request(
    sqlite3* db,
    const std::string_view path,
    const proxy::openai::HeaderMap& headers,
    const std::optional<std::string_view>& socket_ip,
    const FirewallFilterConfig& config
) noexcept {
    FirewallFilterDecision decision;
    if (!is_firewall_protected_path(path)) {
        decision.allow = true;
        return decision;
    }

    decision.client_ip =
        resolve_connection_client_ip(headers, socket_ip, config.trust_proxy_headers, config.trusted_proxy_networks);

    std::optional<std::string_view> ip_for_lookup = std::nullopt;
    if (decision.client_ip.has_value()) {
        ip_for_lookup = std::string_view(*decision.client_ip);
    }

    if (db::is_firewall_ip_allowed(db, ip_for_lookup)) {
        decision.allow = true;
        return decision;
    }

    decision.allow = false;
    decision.status = 403;
    decision.body = proxy::openai::build_error_envelope("ip_forbidden", "Access denied for client IP", "access_error");
    return decision;
}

} // namespace tightrope::server::middleware
