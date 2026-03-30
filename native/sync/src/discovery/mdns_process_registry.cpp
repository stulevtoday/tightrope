#include "discovery/mdns_process_registry.h"

#include <mutex>
#include <string>

#include <boost/container/flat_map.hpp>

#include "sync_logging.h"

namespace tightrope::sync::discovery {

namespace {

std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

boost::container::flat_map<std::string, boost::container::flat_map<std::uint32_t, ProcessMdnsAnnouncement>>&
registry_store() {
    static boost::container::flat_map<std::string, boost::container::flat_map<std::uint32_t, ProcessMdnsAnnouncement>>
        store;
    return store;
}

} // namespace

void register_process_mdns_announcement(const ProcessMdnsAnnouncement& announcement) {
    if (announcement.service_name.empty() || announcement.site_id == 0 || !is_valid_endpoint(announcement.endpoint)) {
        log_discovery_event(SyncLogLevel::Trace, "mdns_process_registry", "register_rejected");
        return;
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    auto& by_site = registry_store()[announcement.service_name];
    by_site[announcement.site_id] = announcement;
    log_discovery_event(
        SyncLogLevel::Trace,
        "mdns_process_registry",
        "register_applied",
        "service=" + announcement.service_name + " site_id=" + std::to_string(announcement.site_id) + " endpoint=" +
            endpoint_to_string(announcement.endpoint) + " total=" + std::to_string(by_site.size()));
}

void unregister_process_mdns_announcement(const std::string_view service_name, const std::uint32_t site_id) {
    if (service_name.empty() || site_id == 0) {
        log_discovery_event(SyncLogLevel::Trace, "mdns_process_registry", "unregister_rejected");
        return;
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    auto by_service = registry_store().find(std::string(service_name));
    if (by_service == registry_store().end()) {
        log_discovery_event(
            SyncLogLevel::Trace,
            "mdns_process_registry",
            "unregister_missing_service",
            "service=" + std::string(service_name) + " site_id=" + std::to_string(site_id));
        return;
    }
    by_service->second.erase(site_id);
    if (by_service->second.empty()) {
        registry_store().erase(by_service);
        log_discovery_event(
            SyncLogLevel::Trace,
            "mdns_process_registry",
            "unregister_applied",
            "service=" + std::string(service_name) + " site_id=" + std::to_string(site_id) + " total=0");
        return;
    }
    log_discovery_event(
        SyncLogLevel::Trace,
        "mdns_process_registry",
        "unregister_applied",
        "service=" + std::string(service_name) + " site_id=" + std::to_string(site_id) + " total=" +
            std::to_string(by_service->second.size()));
}

std::vector<ProcessMdnsAnnouncement> process_mdns_announcements(const std::string_view service_name) {
    std::vector<ProcessMdnsAnnouncement> out;
    if (service_name.empty()) {
        log_discovery_event(SyncLogLevel::Trace, "mdns_process_registry", "list_rejected_empty_service");
        return out;
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    const auto by_service = registry_store().find(std::string(service_name));
    if (by_service == registry_store().end()) {
        log_discovery_event(
            SyncLogLevel::Trace,
            "mdns_process_registry",
            "list_empty",
            "service=" + std::string(service_name));
        return out;
    }

    out.reserve(by_service->second.size());
    for (const auto& [_, announcement] : by_service->second) {
        out.push_back(announcement);
    }
    log_discovery_event(
        SyncLogLevel::Trace,
        "mdns_process_registry",
        "list_complete",
        "service=" + std::string(service_name) + " count=" + std::to_string(out.size()));
    return out;
}

} // namespace tightrope::sync::discovery
