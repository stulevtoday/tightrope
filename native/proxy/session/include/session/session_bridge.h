#pragma once
// HTTP response session bridge

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace tightrope::proxy::session {

struct BridgeSession {
    std::string key;
    std::string upstream_session_id;
    std::int64_t updated_at_ms = 0;
};

class SessionBridge {
public:
    explicit SessionBridge(std::int64_t ttl_ms = 30 * 60 * 1000);

    void upsert(const std::string& key, const std::string& upstream_session_id, std::int64_t now_ms);
    const BridgeSession* find(const std::string& key, std::int64_t now_ms) const;
    std::size_t purge_stale(std::int64_t now_ms);
    std::size_t size() const;

private:
    bool is_stale(const BridgeSession& session, std::int64_t now_ms) const;

    std::int64_t ttl_ms_;
    std::unordered_map<std::string, BridgeSession> sessions_;
};

} // namespace tightrope::proxy::session
