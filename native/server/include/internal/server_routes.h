#pragma once

#include <uwebsockets/App.h>

namespace tightrope::server {

class Runtime;

namespace internal {

void wire_routes(uWS::App& app, Runtime* runtime);

} // namespace internal
} // namespace tightrope::server
