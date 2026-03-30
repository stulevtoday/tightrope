#include "health_controller.h"

namespace tightrope::server::controllers {

HealthStatus get_health(const Runtime& runtime) noexcept {
    return runtime.get_health();
}

} // namespace tightrope::server::controllers
