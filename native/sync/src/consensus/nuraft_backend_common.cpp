#include "consensus/internal/nuraft_backend_components.h"

#include <algorithm>
#include <atomic>
#include <filesystem>

#include "consensus/nuraft_backend.h"

namespace tightrope::sync::consensus::nuraft_backend::internal {

std::vector<std::uint32_t> normalize_members(std::vector<std::uint32_t> members) {
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    return members;
}

nuraft::ptr<nuraft::srv_config> clone_srv_config(const nuraft::srv_config& config) {
    auto clone = nuraft::cs_new<nuraft::srv_config>(
        config.get_id(),
        config.get_dc_id(),
        config.get_endpoint(),
        config.get_aux(),
        config.is_learner(),
        config.get_priority()
    );
    clone->set_new_joiner(config.is_new_joiner());
    return clone;
}

nuraft::ptr<nuraft::cluster_config> clone_cluster_config(const nuraft::cluster_config& config) {
    auto clone = nuraft::cs_new<nuraft::cluster_config>(
        config.get_log_idx(),
        config.get_prev_log_idx(),
        config.is_async_replication()
    );
    clone->set_user_ctx(config.get_user_ctx());
    for (const auto& server : config.get_servers()) {
        clone->get_servers().push_back(clone_srv_config(*server));
    }
    return clone;
}

nuraft::ptr<nuraft::cluster_config> build_cluster_config(
    const std::vector<std::uint32_t>& members,
    const std::uint16_t port_base
) {
    auto config = nuraft::cs_new<nuraft::cluster_config>(0, 0, false);
    for (const auto member : members) {
        config->get_servers().push_back(
            nuraft::cs_new<nuraft::srv_config>(static_cast<nuraft::int32>(member), endpoint_for(member, port_base))
        );
    }
    return config;
}

std::string make_storage_path(const std::string& base_dir, const std::uint32_t node_id, const std::uint16_t port_base) {
    std::error_code ec;
    auto root = base_dir.empty() ? std::filesystem::temp_directory_path(ec) : std::filesystem::path(base_dir);
    if (ec) {
        root = std::filesystem::path(".");
    }
    root /= "tightrope";
    root /= "raft";
    std::filesystem::create_directories(root, ec);

    const auto filename = "nuraft-" + std::to_string(port_base) + "-" + std::to_string(node_id) + ".db";
    return (root / filename).string();
}

nuraft::ptr<nuraft::buffer> copy_blob_to_buffer(const void* blob, const int size) {
    if (blob == nullptr || size <= 0) {
        return nullptr;
    }
    auto out = nuraft::buffer::alloc(static_cast<std::size_t>(size));
    out->put_raw(reinterpret_cast<const nuraft::byte*>(blob), static_cast<std::size_t>(size));
    out->pos(0);
    return out;
}

nuraft::ptr<nuraft::log_entry> make_dummy_entry() {
    auto payload = nuraft::buffer::alloc(sizeof(nuraft::ulong));
    return nuraft::cs_new<nuraft::log_entry>(0, payload);
}

} // namespace tightrope::sync::consensus::nuraft_backend::internal
