#pragma once
// request_log CRUD operations

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace tightrope::db {

struct RequestLogWrite {
    std::optional<std::int64_t> account_id;
    std::string path;
    std::string method;
    int status_code = 0;
    std::optional<std::string> model;
    std::optional<std::string> error_code;
    std::optional<std::string> transport;
    double total_cost = 0.0;
};

struct RequestLogRecord {
    std::int64_t id = 0;
    std::optional<std::int64_t> account_id;
    std::string path;
    std::string method;
    int status_code = 0;
    std::optional<std::string> model;
    std::string requested_at;
    std::optional<std::string> error_code;
    std::optional<std::string> transport;
    double total_cost = 0.0;
};

[[nodiscard]] bool ensure_request_log_schema(sqlite3* db) noexcept;
[[nodiscard]] bool append_request_log(sqlite3* db, const RequestLogWrite& log) noexcept;
[[nodiscard]] std::vector<RequestLogRecord>
list_recent_request_logs(sqlite3* db, std::size_t limit = 50, std::size_t offset = 0) noexcept;
[[nodiscard]] std::optional<std::int64_t>
find_account_id_by_chatgpt_account_id(sqlite3* db, std::string_view chatgpt_account_id) noexcept;

} // namespace tightrope::db
