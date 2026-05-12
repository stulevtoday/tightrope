#pragma once
// sessions API controller

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct StickySessionPayload {
    std::string session_key;
    std::string account_id;
    std::string kind = "sticky_thread";
    std::int64_t updated_at_ms = 0;
    std::int64_t expires_at_ms = 0;
};

struct StickySessionsResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::int64_t generated_at_ms = 0;
    std::vector<StickySessionPayload> sessions;
};

struct StickySessionsPurgeResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::int64_t generated_at_ms = 0;
    std::size_t purged = 0;
};

StickySessionsResponse list_sticky_sessions(std::size_t limit = 500, std::size_t offset = 0, sqlite3* db = nullptr);
StickySessionsPurgeResponse purge_stale_sticky_sessions(sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
