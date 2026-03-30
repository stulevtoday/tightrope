#include "session_manager.h"

#include <algorithm>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace tightrope::auth::dashboard {

DashboardSessionManager::DashboardSessionManager(const std::int64_t ttl_ms) : ttl_ms_(std::max<std::int64_t>(1, ttl_ms)) {}

std::string DashboardSessionManager::create(
    const bool password_verified,
    const bool totp_verified,
    const std::int64_t now_ms
) {
    const auto session_id = boost::uuids::to_string(boost::uuids::random_generator()());
    sessions_[session_id] = DashboardSessionState{
        .expires_at_ms = now_ms + ttl_ms_,
        .password_verified = password_verified,
        .totp_verified = totp_verified,
    };
    return session_id;
}

std::optional<DashboardSessionState> DashboardSessionManager::get(
    const std::string_view session_id,
    const std::int64_t now_ms
) const {
    if (session_id.empty()) {
        return std::nullopt;
    }
    const auto it = sessions_.find(std::string(session_id));
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    if (now_ms >= it->second.expires_at_ms) {
        return std::nullopt;
    }
    return it->second;
}

void DashboardSessionManager::erase(const std::string_view session_id) {
    if (session_id.empty()) {
        return;
    }
    sessions_.erase(std::string(session_id));
}

std::size_t DashboardSessionManager::purge_expired(const std::int64_t now_ms) {
    std::size_t removed = 0;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now_ms >= it->second.expires_at_ms) {
            it = sessions_.erase(it);
            ++removed;
            continue;
        }
        ++it;
    }
    return removed;
}

} // namespace tightrope::auth::dashboard
