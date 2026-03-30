#include "source_contract_catalog.h"

#include "contracts/internal/source_contract_internal.h"

namespace tightrope::tests::contracts {

std::optional<SourceRouteContract> find_source_route_contract(
    const std::vector<SourceRouteContract>& routes,
    std::string_view method,
    std::string_view path
) {
    const std::string normalized_method = internal::to_upper_copy(std::string(method));
    for (const auto& route : routes) {
        if (route.method == normalized_method && route.path == path) {
            return route;
        }
    }
    return std::nullopt;
}

} // namespace tightrope::tests::contracts
