#pragma once

#include <uwebsockets/App.h>

namespace tightrope::server::internal::admin {

void wire_settings_routes(uWS::App& app);
void wire_api_keys_routes(uWS::App& app);
void wire_accounts_routes(uWS::App& app);
void wire_oauth_routes(uWS::App& app);

} // namespace tightrope::server::internal::admin
