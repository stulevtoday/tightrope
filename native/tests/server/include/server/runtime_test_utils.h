#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tightrope::tests::server {

std::uint16_t next_runtime_port();
std::string make_temp_runtime_db_path();

class EnvVarGuard final {
  public:
    explicit EnvVarGuard(const char* key);
    ~EnvVarGuard();
    EnvVarGuard(const EnvVarGuard&) = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

    [[nodiscard]] bool set(std::string_view value) const;

  private:
    std::string key_;
    std::optional<std::string> original_{};
};

std::string send_raw_http(std::uint16_t port, std::string_view request, int max_reads = 64);
std::string send_raw_http_to_host(
    std::string_view host,
    std::uint16_t port,
    std::string_view request,
    int max_reads = 64
);
std::string http_body(std::string_view http_response);
std::optional<std::string> http_header_value(std::string_view http_response, std::string_view header_name);

} // namespace tightrope::tests::server
