#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "openai/upstream_headers.h"

namespace tightrope::proxy::session {

struct StickyAffinityResolution {
    std::string sticky_key;
    std::string account_id;
    bool from_header = false;
    bool from_persistence = false;
};

struct UpstreamAccountCredentials {
    std::string account_id;
    std::string access_token;
};

StickyAffinityResolution
resolve_sticky_affinity(const std::string& raw_request_body, const openai::HeaderMap& inbound_headers);
void persist_sticky_affinity(const StickyAffinityResolution& resolution);
std::optional<UpstreamAccountCredentials> resolve_upstream_account_credentials(
    std::string_view preferred_account_id
);
std::optional<UpstreamAccountCredentials> refresh_upstream_account_credentials(std::string_view account_id);

} // namespace tightrope::proxy::session
