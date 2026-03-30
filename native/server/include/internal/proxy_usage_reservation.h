#pragma once

#include <string>
#include <string_view>

#include <sqlite3.h>

#include "middleware/api_key_filter.h"

namespace tightrope::server::internal {

struct ProxyUsageReservation {
    bool active = false;
    std::string request_id;
};

[[nodiscard]] ProxyUsageReservation reserve_api_key_usage(
    sqlite3* db,
    const middleware::ApiKeyModelPolicy& policy,
    std::string_view request_id
) noexcept;

void settle_api_key_usage(
    sqlite3* db,
    const ProxyUsageReservation& reservation,
    bool success
) noexcept;

} // namespace tightrope::server::internal
