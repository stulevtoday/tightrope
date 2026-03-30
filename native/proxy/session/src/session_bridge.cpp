#include "session_bridge.h"

#include <algorithm>

namespace tightrope::proxy::session {

SessionBridge::SessionBridge(const std::int64_t ttl_ms) : ttl_ms_(std::max<std::int64_t>(1, ttl_ms)) {}

void SessionBridge::upsert(const std::string& key, const std::string& upstream_session_id, const std::int64_t now_ms) {
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        sessions_.emplace(
            key,
            BridgeSession{
                .key = key,
                .upstream_session_id = upstream_session_id,
                .updated_at_ms = now_ms,
            }
        );
        return;
    }

    it->second.upstream_session_id = upstream_session_id;
    it->second.updated_at_ms = now_ms;
}

const BridgeSession* SessionBridge::find(const std::string& key, const std::int64_t now_ms) const {
    const auto it = sessions_.find(key);
    if (it == sessions_.end() || is_stale(it->second, now_ms)) {
        return nullptr;
    }
    return &it->second;
}

std::size_t SessionBridge::purge_stale(const std::int64_t now_ms) {
    std::size_t purged = 0;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (is_stale(it->second, now_ms)) {
            it = sessions_.erase(it);
            ++purged;
            continue;
        }
        ++it;
    }
    return purged;
}

std::size_t SessionBridge::size() const {
    return sessions_.size();
}

bool SessionBridge::is_stale(const BridgeSession& session, const std::int64_t now_ms) const {
    if (now_ms < session.updated_at_ms) {
        return false;
    }
    return (now_ms - session.updated_at_ms) >= ttl_ms_;
}

} // namespace tightrope::proxy::session
