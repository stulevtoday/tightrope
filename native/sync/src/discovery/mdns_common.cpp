#include "discovery/mdns_common.h"

#include <algorithm>
#include <cctype>

namespace tightrope::sync::discovery {

std::string canonical_mdns_service_name(std::string_view service_name) {
    std::string service(service_name);
    if (service.empty()) {
        service = "_tightrope-sync._tcp";
    }
    if (service.find(".local") == std::string::npos) {
        service += ".local";
    }
    if (!service.empty() && service.back() != '.') {
        service.push_back('.');
    }
    return service;
}

std::string canonical_mdns_hostname(
    const std::string_view host, const std::uint32_t site_id, const bool host_is_ip_literal) {
    if (host_is_ip_literal) {
        return "tightrope-site-" + std::to_string(site_id) + ".local.";
    }

    std::string result(host);
    if (result.empty()) {
        result = "tightrope-site-" + std::to_string(site_id) + ".local";
    }
    if (result.find('.') == std::string::npos) {
        result += ".local";
    }
    if (result.back() != '.') {
        result.push_back('.');
    }
    return result;
}

std::string trim_mdns_trailing_dot(const std::string_view value) {
    if (!value.empty() && value.back() == '.') {
        return std::string(value.substr(0, value.size() - 1));
    }
    return std::string(value);
}

std::string lowercase_ascii(const std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

} // namespace tightrope::sync::discovery
