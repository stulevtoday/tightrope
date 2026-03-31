#include "account_traffic.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace tightrope::proxy {

namespace {

std::mutex& traffic_mutex() {
    static auto* value = new std::mutex();
    return *value;
}

std::unordered_map<std::string, AccountTrafficSnapshot>& traffic_by_account() {
    static auto* value = new std::unordered_map<std::string, AccountTrafficSnapshot>();
    return *value;
}

AccountTrafficUpdateCallback& update_callback() {
    static auto* value = new AccountTrafficUpdateCallback();
    return *value;
}

std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void record_traffic(std::string_view account_id, std::size_t bytes, bool egress) {
    if (account_id.empty() || bytes == 0) {
        return;
    }

    AccountTrafficSnapshot snapshot;
    AccountTrafficUpdateCallback callback;
    {
        std::lock_guard<std::mutex> lock(traffic_mutex());
        auto& entry = traffic_by_account()[std::string(account_id)];
        if (entry.account_id.empty()) {
            entry.account_id = std::string(account_id);
        }
        if (egress) {
            entry.up_bytes += static_cast<std::uint64_t>(bytes);
            entry.last_up_at_ms = now_ms();
        } else {
            entry.down_bytes += static_cast<std::uint64_t>(bytes);
            entry.last_down_at_ms = now_ms();
        }
        snapshot = entry;
        callback = update_callback();
    }

    if (callback) {
        callback(snapshot);
    }
}

} // namespace

void record_account_upstream_egress(const std::string_view account_id, const std::size_t bytes) {
    record_traffic(account_id, bytes, true);
}

void record_account_upstream_ingress(const std::string_view account_id, const std::size_t bytes) {
    record_traffic(account_id, bytes, false);
}

std::vector<AccountTrafficSnapshot> snapshot_account_traffic() {
    std::vector<AccountTrafficSnapshot> snapshots;
    {
        std::lock_guard<std::mutex> lock(traffic_mutex());
        snapshots.reserve(traffic_by_account().size());
        for (const auto& [account_id, snapshot] : traffic_by_account()) {
            static_cast<void>(account_id);
            snapshots.push_back(snapshot);
        }
    }
    std::sort(
        snapshots.begin(),
        snapshots.end(),
        [](const AccountTrafficSnapshot& lhs, const AccountTrafficSnapshot& rhs) {
            return lhs.account_id < rhs.account_id;
        }
    );
    return snapshots;
}

void clear_account_traffic_for_testing() {
    std::lock_guard<std::mutex> lock(traffic_mutex());
    traffic_by_account().clear();
}

void set_account_traffic_update_callback(AccountTrafficUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(traffic_mutex());
    update_callback() = std::move(callback);
}

void clear_account_traffic_update_callback() {
    std::lock_guard<std::mutex> lock(traffic_mutex());
    update_callback() = {};
}

} // namespace tightrope::proxy
