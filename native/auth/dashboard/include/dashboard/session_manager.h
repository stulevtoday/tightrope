#pragma once
// Cookie session management

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tightrope::auth::dashboard {

struct DashboardSessionState {
    std::int64_t expires_at_ms = 0;
    bool password_verified = false;
    bool totp_verified = false;
};

class DashboardSessionManager {
public:
    explicit DashboardSessionManager(std::int64_t ttl_ms = 12 * 60 * 60 * 1000);

    [[nodiscard]] std::string create(bool password_verified, bool totp_verified, std::int64_t now_ms);
    [[nodiscard]] std::optional<DashboardSessionState> get(std::string_view session_id, std::int64_t now_ms) const;
    void erase(std::string_view session_id);
    std::size_t purge_expired(std::int64_t now_ms);

private:
    std::int64_t ttl_ms_ = 12 * 60 * 60 * 1000;
    std::unordered_map<std::string, DashboardSessionState> sessions_;
};

} // namespace tightrope::auth::dashboard
