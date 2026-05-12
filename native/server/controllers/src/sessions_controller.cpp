#include "sessions_controller.h"

#include <algorithm>

#include "controller_db.h"
#include "repositories/session_repo.h"
#include "time/clock.h"

namespace tightrope::server::controllers {

namespace {

constexpr std::size_t kMaxLimit = 2000;

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

std::size_t clamped_limit(const std::size_t requested) {
    if (requested == 0) {
        return 1;
    }
    return std::min(requested, kMaxLimit);
}

StickySessionPayload to_payload(const db::ProxyStickySessionRecord& record) {
    return {
        .session_key = record.session_key,
        .account_id = record.account_id,
        .kind = record.kind,
        .updated_at_ms = record.updated_at_ms,
        .expires_at_ms = record.expires_at_ms,
    };
}

} // namespace

StickySessionsResponse list_sticky_sessions(const std::size_t limit, const std::size_t offset, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
            .generated_at_ms = now_ms(),
        };
    }

    StickySessionsResponse response{
        .status = 200,
        .generated_at_ms = now_ms(),
    };
    for (const auto& record : db::list_proxy_sticky_sessions(handle.db, clamped_limit(limit), offset)) {
        response.sessions.push_back(to_payload(record));
    }
    return response;
}

StickySessionsPurgeResponse purge_stale_sticky_sessions(sqlite3* db) {
    auto handle = open_controller_db(db);
    const auto generated_at_ms = now_ms();
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
            .generated_at_ms = generated_at_ms,
        };
    }

    return {
        .status = 200,
        .generated_at_ms = generated_at_ms,
        .purged = db::purge_expired_proxy_sticky_sessions(handle.db, generated_at_ms),
    };
}

} // namespace tightrope::server::controllers
