#pragma once

#include "server.h"

// health API controller

namespace tightrope::server::controllers {

[[nodiscard]] HealthStatus get_health(const Runtime& runtime) noexcept;

} // namespace tightrope::server::controllers
