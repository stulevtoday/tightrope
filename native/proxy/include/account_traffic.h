#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::proxy {

struct AccountTrafficSnapshot {
    std::string account_id;
    std::uint64_t up_bytes = 0;
    std::uint64_t down_bytes = 0;
    std::int64_t last_up_at_ms = 0;
    std::int64_t last_down_at_ms = 0;
};

using AccountTrafficUpdateCallback = std::function<void(const AccountTrafficSnapshot&)>;

void record_account_upstream_egress(std::string_view account_id, std::size_t bytes);
void record_account_upstream_ingress(std::string_view account_id, std::size_t bytes);
[[nodiscard]] std::vector<AccountTrafficSnapshot> snapshot_account_traffic();
void clear_account_traffic_for_testing();
void set_account_traffic_update_callback(AccountTrafficUpdateCallback callback);
void clear_account_traffic_update_callback();

} // namespace tightrope::proxy
