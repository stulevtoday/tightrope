#include "internal/proxy_usage_reservation.h"

#include <string>

#include "repositories/api_key_repo.h"

namespace tightrope::server::internal {

ProxyUsageReservation reserve_api_key_usage(
    sqlite3* db,
    const middleware::ApiKeyModelPolicy& policy,
    const std::string_view request_id
) noexcept {
    ProxyUsageReservation reservation{};
    if (db == nullptr || request_id.empty() || !policy.key_id.has_value() || policy.key_id->empty()) {
        return reservation;
    }

    const auto created = db::create_api_key_usage_reservation(db, *policy.key_id, request_id, {});
    if (!created.has_value()) {
        return reservation;
    }

    reservation.active = true;
    reservation.request_id = std::string(request_id);
    return reservation;
}

void settle_api_key_usage(
    sqlite3* db,
    const ProxyUsageReservation& reservation,
    const bool success
) noexcept {
    if (db == nullptr || !reservation.active || reservation.request_id.empty()) {
        return;
    }

    const auto next_status = success ? std::string_view("settled") : std::string_view("released");
    (void)db::transition_api_key_usage_reservation_status(db, reservation.request_id, "reserved", next_status);
}

} // namespace tightrope::server::internal
