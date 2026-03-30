#pragma once

#include "config.h"

// Parse env vars and config files

namespace tightrope::config {

[[nodiscard]] Config load_config(const ConfigOverrides& overrides = {});

} // namespace tightrope::config
