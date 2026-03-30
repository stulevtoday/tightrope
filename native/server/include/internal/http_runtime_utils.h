#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uwebsockets/App.h>

#include "openai/upstream_headers.h"

namespace tightrope::server::internal::http {

[[nodiscard]] std::string to_string_safe(std::string_view value);
[[nodiscard]] std::optional<std::string> maybe_string(std::string_view value);
[[nodiscard]] std::string decode_percent_escapes(std::string_view raw);
[[nodiscard]] std::string status_line(int status);
[[nodiscard]] proxy::openai::HeaderMap request_headers(uWS::HttpRequest* req);
[[nodiscard]] bool
header_contains(const proxy::openai::HeaderMap& headers, std::string_view key, std::string_view needle);
[[nodiscard]] std::optional<std::string_view> socket_ip(uWS::HttpResponse<false>* res);

void write_http(
    uWS::HttpResponse<false>* res,
    int status,
    std::string_view content_type,
    std::string_view body,
    std::string_view request_id = "",
    const proxy::openai::HeaderMap* extra_headers = nullptr
);
void write_json(
    uWS::HttpResponse<false>* res,
    int status,
    const std::string& body,
    std::string_view request_id = "",
    const proxy::openai::HeaderMap* extra_headers = nullptr
);
void write_sse(
    uWS::HttpResponse<false>* res,
    int status,
    const std::vector<std::string>& events,
    std::string_view request_id = "",
    const proxy::openai::HeaderMap* extra_headers = nullptr
);

template <typename Fn>
void read_request_body(uWS::HttpResponse<false>* res, Fn&& fn) {
    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted] { *aborted = true; });
    res->onData(
        [body, aborted, fn = std::forward<Fn>(fn)](const std::string_view chunk, const bool is_last) mutable {
            if (*aborted) {
                return;
            }
            if (!chunk.empty()) {
                body->append(chunk.data(), chunk.size());
            }
            if (!is_last) {
                return;
            }
            fn(std::move(*body));
        }
    );
}

} // namespace tightrope::server::internal::http
