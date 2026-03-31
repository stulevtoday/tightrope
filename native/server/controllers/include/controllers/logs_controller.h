#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct RequestLogPayload {
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

struct RequestLogsResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::size_t limit = 0;
    std::size_t offset = 0;
    std::vector<RequestLogPayload> logs;
};

RequestLogsResponse list_request_logs(std::size_t limit = 200, std::size_t offset = 0, sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
