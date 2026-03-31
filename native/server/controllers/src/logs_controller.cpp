#include "logs_controller.h"

#include <algorithm>

#include "controller_db.h"
#include "repositories/request_log_repo.h"

namespace tightrope::server::controllers {

namespace {

constexpr std::size_t kMaxLimit = 500;

std::size_t clamped_limit(const std::size_t requested) {
    if (requested == 0) {
        return 1;
    }
    return std::min(requested, kMaxLimit);
}

RequestLogPayload to_payload(const db::RequestLogRecord& record) {
    return {
        .id = record.id,
        .account_id = record.account_id,
        .path = record.path,
        .method = record.method,
        .status_code = record.status_code,
        .model = record.model,
        .requested_at = record.requested_at,
        .error_code = record.error_code,
        .transport = record.transport,
        .total_cost = record.total_cost,
    };
}

} // namespace

RequestLogsResponse list_request_logs(const std::size_t limit, const std::size_t offset, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
            .limit = clamped_limit(limit),
            .offset = offset,
        };
    }

    RequestLogsResponse response;
    response.status = 200;
    response.limit = clamped_limit(limit);
    response.offset = offset;

    for (const auto& record : db::list_recent_request_logs(handle.db, response.limit, offset)) {
        response.logs.push_back(to_payload(record));
    }
    return response;
}

} // namespace tightrope::server::controllers
