#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "discovery/peer_endpoint.h"

namespace tightrope::sync::discovery {

struct ProcessMdnsAnnouncement {
    std::string service_name;
    std::string cluster_name;
    std::uint32_t site_id = 0;
    PeerEndpoint endpoint;
};

void register_process_mdns_announcement(const ProcessMdnsAnnouncement& announcement);
void unregister_process_mdns_announcement(std::string_view service_name, std::uint32_t site_id);
std::vector<ProcessMdnsAnnouncement> process_mdns_announcements(std::string_view service_name);

} // namespace tightrope::sync::discovery
