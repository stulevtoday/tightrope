#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::sync::consensus::nuraft_backend {

class Backend {
public:
    Backend(std::uint32_t node_id, std::vector<std::uint32_t> members, std::uint16_t port_base);
    ~Backend();

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] bool is_leader() const noexcept;
    [[nodiscard]] std::int32_t leader_id() const noexcept;
    [[nodiscard]] std::uint64_t term() const noexcept;
    [[nodiscard]] std::uint64_t committed_index() const noexcept;
    [[nodiscard]] std::uint64_t last_log_index() const noexcept;
    [[nodiscard]] std::size_t committed_entry_count() const noexcept;
    [[nodiscard]] bool trigger_election() noexcept;
    [[nodiscard]] std::optional<std::uint64_t> append_payload(std::string_view payload);

private:
    class Impl;
    Impl* impl_ = nullptr;
};

[[nodiscard]] std::string endpoint_for(std::uint32_t node_id, std::uint16_t port_base);

} // namespace tightrope::sync::consensus::nuraft_backend
