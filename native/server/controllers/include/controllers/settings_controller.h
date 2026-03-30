#pragma once
// settings API controller

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct DashboardSettingsPayload {
    std::string theme = "auto";
    bool sticky_threads_enabled = false;
    std::string upstream_stream_transport = "default";
    bool prefer_earlier_reset_accounts = false;
    std::string routing_strategy = "usage_weighted";
    std::int64_t openai_cache_affinity_max_age_seconds = 300;
    bool import_without_overwrite = false;
    bool totp_required_on_login = false;
    bool totp_configured = false;
    bool api_key_auth_enabled = false;
    double routing_headroom_weight_primary = 0.35;
    double routing_headroom_weight_secondary = 0.65;
    double routing_score_alpha = 0.3;
    double routing_score_beta = 0.25;
    double routing_score_gamma = 0.2;
    double routing_score_delta = 0.2;
    double routing_score_zeta = 0.05;
    double routing_score_eta = 1.0;
    double routing_success_rate_rho = 2.0;
    std::string sync_cluster_name = "default";
    std::int64_t sync_site_id = 1;
    std::int64_t sync_port = 9400;
    bool sync_discovery_enabled = true;
    std::int64_t sync_interval_seconds = 5;
    std::string sync_conflict_resolution = "lww";
    std::int64_t sync_journal_retention_days = 30;
    bool sync_tls_enabled = true;
};

struct DashboardSettingsUpdate {
    std::optional<std::string> theme;
    bool sticky_threads_enabled = false;
    std::optional<std::string> upstream_stream_transport;
    bool prefer_earlier_reset_accounts = false;
    std::optional<std::string> routing_strategy;
    std::optional<std::int64_t> openai_cache_affinity_max_age_seconds;
    std::optional<bool> import_without_overwrite;
    std::optional<bool> totp_required_on_login;
    std::optional<bool> api_key_auth_enabled;
    std::optional<double> routing_headroom_weight_primary;
    std::optional<double> routing_headroom_weight_secondary;
    std::optional<double> routing_score_alpha;
    std::optional<double> routing_score_beta;
    std::optional<double> routing_score_gamma;
    std::optional<double> routing_score_delta;
    std::optional<double> routing_score_zeta;
    std::optional<double> routing_score_eta;
    std::optional<double> routing_success_rate_rho;
    std::optional<std::string> sync_cluster_name;
    std::optional<std::int64_t> sync_site_id;
    std::optional<std::int64_t> sync_port;
    std::optional<bool> sync_discovery_enabled;
    std::optional<std::int64_t> sync_interval_seconds;
    std::optional<std::string> sync_conflict_resolution;
    std::optional<std::int64_t> sync_journal_retention_days;
    std::optional<bool> sync_tls_enabled;
};

struct DashboardSettingsResponse {
    int status = 500;
    std::string code;
    std::string message;
    DashboardSettingsPayload settings;
};

struct RuntimeConnectAddressResponse {
    int status = 500;
    std::string connect_address;
};

DashboardSettingsResponse get_settings(sqlite3* db = nullptr);
DashboardSettingsResponse update_settings(const DashboardSettingsUpdate& update, sqlite3* db = nullptr);
RuntimeConnectAddressResponse get_runtime_connect_address(std::string_view request_host);

} // namespace tightrope::server::controllers
