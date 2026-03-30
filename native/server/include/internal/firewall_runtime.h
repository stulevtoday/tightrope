#pragma once

#include <string_view>

#include <uwebsockets/App.h>

#include "openai/upstream_headers.h"

namespace tightrope::server::internal::firewall {

[[nodiscard]] bool allow_request_or_write_denial(
    uWS::HttpResponse<false>* res,
    std::string_view path,
    const proxy::openai::HeaderMap& headers
);

void wire_routes(uWS::App& app);

} // namespace tightrope::server::internal::firewall

