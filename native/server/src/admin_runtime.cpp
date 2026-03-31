#include "internal/admin_runtime.h"

#include "internal/admin_runtime_parts.h"

namespace tightrope::server::internal::admin {

void wire_routes(uWS::App& app) {
    wire_settings_routes(app);
    wire_api_keys_routes(app);
    wire_accounts_routes(app);
    wire_logs_routes(app);
    wire_oauth_routes(app);
}

} // namespace tightrope::server::internal::admin
