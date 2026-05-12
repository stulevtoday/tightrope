#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "openai/upstream_headers.h"

namespace tightrope::proxy::session {

struct StickyAffinityResolution {
    std::string sticky_key;
    std::string sticky_kind = "sticky_thread";
    std::string account_id;
    std::string request_model;
    bool from_header = false;
    bool from_persistence = false;
};

struct UpstreamAccountCredentials {
    std::string account_id;
    std::string access_token;
    std::int64_t internal_account_id = 0;
};

StickyAffinityResolution
resolve_sticky_affinity(const std::string& raw_request_body, const openai::HeaderMap& inbound_headers);
void persist_sticky_affinity(const StickyAffinityResolution& resolution);
std::optional<UpstreamAccountCredentials> resolve_upstream_account_credentials(
    std::string_view preferred_account_id,
    std::string_view request_model = {},
    bool strict_preferred_only = false
);
std::string resolve_upstream_stream_transport_setting();
std::optional<UpstreamAccountCredentials> refresh_upstream_account_credentials(std::string_view account_id);
bool upstream_account_record_exists(std::string_view account_id);
bool mark_upstream_account_unusable(std::string_view account_id);
bool mark_upstream_account_exhausted(std::string_view account_id, std::string_view status);
bool account_is_in_active_lock_pool(std::string_view account_id);

} // namespace tightrope::proxy::session
