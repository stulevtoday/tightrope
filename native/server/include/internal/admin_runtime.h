#pragma once

#include <uwebsockets/App.h>

namespace tightrope::server::internal::admin {

void wire_routes(uWS::App& app);

} // namespace tightrope::server::internal::admin

