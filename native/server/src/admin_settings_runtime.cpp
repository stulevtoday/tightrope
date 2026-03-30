#include "internal/admin_runtime_parts.h"

#include <optional>
#include <string>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "controllers/settings_controller.h"

namespace tightrope::server::internal::admin {

namespace {

std::string settings_json(const controllers::DashboardSettingsPayload& settings) {
    return std::string(R"({"theme":)") + core::text::quote_json_string(settings.theme) +
           R"(,"stickyThreadsEnabled":)" + bool_json(settings.sticky_threads_enabled) +
           R"(,"upstreamStreamTransport":)" + core::text::quote_json_string(settings.upstream_stream_transport) +
           R"(,"preferEarlierResetAccounts":)" + bool_json(settings.prefer_earlier_reset_accounts) +
           R"(,"routingStrategy":)" + core::text::quote_json_string(settings.routing_strategy) +
           R"(,"openaiCacheAffinityMaxAgeSeconds":)" +
           std::to_string(settings.openai_cache_affinity_max_age_seconds) + R"(,"importWithoutOverwrite":)" +
           bool_json(settings.import_without_overwrite) + R"(,"totpRequiredOnLogin":)" +
           bool_json(settings.totp_required_on_login) + R"(,"totpConfigured":)" + bool_json(settings.totp_configured) +
           R"(,"apiKeyAuthEnabled":)" + bool_json(settings.api_key_auth_enabled) +
           R"(,"routingHeadroomWeightPrimary":)" + std::to_string(settings.routing_headroom_weight_primary) +
           R"(,"routingHeadroomWeightSecondary":)" + std::to_string(settings.routing_headroom_weight_secondary) +
           R"(,"routingScoreAlpha":)" + std::to_string(settings.routing_score_alpha) + R"(,"routingScoreBeta":)" +
           std::to_string(settings.routing_score_beta) + R"(,"routingScoreGamma":)" +
           std::to_string(settings.routing_score_gamma) + R"(,"routingScoreDelta":)" +
           std::to_string(settings.routing_score_delta) + R"(,"routingScoreZeta":)" +
           std::to_string(settings.routing_score_zeta) + R"(,"routingScoreEta":)" +
           std::to_string(settings.routing_score_eta) + R"(,"routingSuccessRateRho":)" +
           std::to_string(settings.routing_success_rate_rho) + R"(,"syncClusterName":)" +
           core::text::quote_json_string(settings.sync_cluster_name) + R"(,"syncSiteId":)" +
           std::to_string(settings.sync_site_id) + R"(,"syncPort":)" + std::to_string(settings.sync_port) +
           R"(,"syncDiscoveryEnabled":)" + bool_json(settings.sync_discovery_enabled) +
           R"(,"syncIntervalSeconds":)" + std::to_string(settings.sync_interval_seconds) +
           R"(,"syncConflictResolution":)" + core::text::quote_json_string(settings.sync_conflict_resolution) +
           R"(,"syncJournalRetentionDays":)" + std::to_string(settings.sync_journal_retention_days) +
           R"(,"syncTlsEnabled":)" + bool_json(settings.sync_tls_enabled) + "}";
}

} // namespace

void wire_settings_routes(uWS::App& app) {
    app.get("/api/settings", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::get_settings();
        if (response.status == 200) {
            http::write_json(res, 200, settings_json(response.settings));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.put("/api/settings", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            const auto parsed = parse_json_object(body);
            if (!parsed.has_value()) {
                http::write_json(res, 400, dashboard_error_json("invalid_request", "Invalid JSON payload"));
                return;
            }

            controllers::DashboardSettingsUpdate update{};
            update.theme = json_string(*parsed, "theme");
            update.sticky_threads_enabled = json_bool(*parsed, "stickyThreadsEnabled")
                                                .or_else([&] { return json_bool(*parsed, "sticky_threads_enabled"); })
                                                .value_or(false);
            update.upstream_stream_transport = json_string(*parsed, "upstreamStreamTransport")
                                                   .or_else([&] { return json_string(*parsed, "upstream_stream_transport"); });
            update.prefer_earlier_reset_accounts = json_bool(*parsed, "preferEarlierResetAccounts")
                                                       .or_else([&] {
                                                           return json_bool(*parsed, "prefer_earlier_reset_accounts");
                                                       })
                                                       .value_or(false);
            update.routing_strategy = json_string(*parsed, "routingStrategy")
                                          .or_else([&] { return json_string(*parsed, "routing_strategy"); });
            update.openai_cache_affinity_max_age_seconds = json_int64(*parsed, "openaiCacheAffinityMaxAgeSeconds")
                                                               .or_else([&] {
                                                                   return json_int64(
                                                                       *parsed,
                                                                       "openai_cache_affinity_max_age_seconds"
                                                                   );
                                                               });
            update.import_without_overwrite = json_bool(*parsed, "importWithoutOverwrite")
                                                  .or_else([&] { return json_bool(*parsed, "import_without_overwrite"); });
            update.totp_required_on_login = json_bool(*parsed, "totpRequiredOnLogin")
                                                .or_else([&] { return json_bool(*parsed, "totp_required_on_login"); });
            update.api_key_auth_enabled = json_bool(*parsed, "apiKeyAuthEnabled")
                                              .or_else([&] { return json_bool(*parsed, "api_key_auth_enabled"); });
            update.routing_headroom_weight_primary = json_double(*parsed, "routingHeadroomWeightPrimary")
                                                         .or_else([&] {
                                                             return json_double(
                                                                 *parsed,
                                                                 "routing_headroom_weight_primary"
                                                             );
                                                         });
            update.routing_headroom_weight_secondary = json_double(*parsed, "routingHeadroomWeightSecondary")
                                                           .or_else([&] {
                                                               return json_double(
                                                                   *parsed,
                                                                   "routing_headroom_weight_secondary"
                                                               );
                                                           });
            update.routing_score_alpha = json_double(*parsed, "routingScoreAlpha")
                                             .or_else([&] { return json_double(*parsed, "routing_score_alpha"); });
            update.routing_score_beta = json_double(*parsed, "routingScoreBeta")
                                            .or_else([&] { return json_double(*parsed, "routing_score_beta"); });
            update.routing_score_gamma = json_double(*parsed, "routingScoreGamma")
                                             .or_else([&] { return json_double(*parsed, "routing_score_gamma"); });
            update.routing_score_delta = json_double(*parsed, "routingScoreDelta")
                                             .or_else([&] { return json_double(*parsed, "routing_score_delta"); });
            update.routing_score_zeta = json_double(*parsed, "routingScoreZeta")
                                            .or_else([&] { return json_double(*parsed, "routing_score_zeta"); });
            update.routing_score_eta = json_double(*parsed, "routingScoreEta")
                                           .or_else([&] { return json_double(*parsed, "routing_score_eta"); });
            update.routing_success_rate_rho = json_double(*parsed, "routingSuccessRateRho")
                                                  .or_else([&] {
                                                      return json_double(
                                                          *parsed,
                                                          "routing_success_rate_rho"
                                                      );
                                                  });
            update.sync_cluster_name = json_string(*parsed, "syncClusterName")
                                           .or_else([&] { return json_string(*parsed, "sync_cluster_name"); });
            update.sync_site_id = json_int64(*parsed, "syncSiteId")
                                      .or_else([&] { return json_int64(*parsed, "sync_site_id"); });
            update.sync_port = json_int64(*parsed, "syncPort")
                                   .or_else([&] { return json_int64(*parsed, "sync_port"); });
            update.sync_discovery_enabled = json_bool(*parsed, "syncDiscoveryEnabled")
                                                .or_else([&] {
                                                    return json_bool(*parsed, "sync_discovery_enabled");
                                                });
            update.sync_interval_seconds = json_int64(*parsed, "syncIntervalSeconds")
                                               .or_else([&] {
                                                   return json_int64(*parsed, "sync_interval_seconds");
                                               });
            update.sync_conflict_resolution = json_string(*parsed, "syncConflictResolution")
                                                  .or_else([&] {
                                                      return json_string(*parsed, "sync_conflict_resolution");
                                                  });
            update.sync_journal_retention_days = json_int64(*parsed, "syncJournalRetentionDays")
                                                     .or_else([&] {
                                                         return json_int64(
                                                             *parsed,
                                                             "sync_journal_retention_days"
                                                         );
                                                     });
            update.sync_tls_enabled = json_bool(*parsed, "syncTlsEnabled")
                                          .or_else([&] { return json_bool(*parsed, "sync_tls_enabled"); });

            const auto response = controllers::update_settings(update);
            if (response.status == 200) {
                http::write_json(res, 200, settings_json(response.settings));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.get("/api/settings/runtime/connect-address", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto response = controllers::get_runtime_connect_address(request_host(req));
        http::write_json(
            res,
            response.status,
            std::string(R"({"connectAddress":)") + core::text::quote_json_string(response.connect_address) + "}"
        );
    });
}

} // namespace tightrope::server::internal::admin
