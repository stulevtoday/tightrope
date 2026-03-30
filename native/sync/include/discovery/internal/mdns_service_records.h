#pragma once

#include <array>
#include <optional>
#include <string>

#include "mdns.h"

#include "discovery/mdns_publisher.h"

namespace tightrope::sync::discovery::internal {

struct ServiceRecords {
    std::string service_name;
    std::string instance_name;
    std::string host_name;
    std::string txt_cluster;
    std::string txt_site_id;
    std::string txt_sync_port;

    mdns_record_t ptr_record{};
    mdns_record_t srv_record{};
    mdns_record_t a_record{};
    mdns_record_t aaaa_record{};
    std::array<mdns_record_t, 3> txt_records{};
    std::array<mdns_record_t, 6> additional_records{};
    std::size_t additional_count = 0;
};

std::optional<ServiceRecords> build_service_records(
    const std::string& raw_service_name,
    const ServiceAnnouncement& announcement
);

} // namespace tightrope::sync::discovery::internal
