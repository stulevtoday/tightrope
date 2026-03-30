#pragma once
// Publish _tightrope-sync._tcp service

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "mdns.h"

#include "peer_endpoint.h"

namespace tightrope::sync::discovery {

class MdnsRuntime;

struct ServiceAnnouncement {
    std::string cluster_name;
    std::uint32_t site_id = 0;
    PeerEndpoint endpoint;
};

class MdnsPublisher {
public:
    explicit MdnsPublisher(
        std::string service_name = "_tightrope-sync._tcp", MdnsRuntime* runtime = nullptr);
    ~MdnsPublisher();

    bool publish(const ServiceAnnouncement& announcement);
    void unpublish();

    bool published() const;
    const std::optional<ServiceAnnouncement>& current() const;
    const std::string& service_name() const;

private:
    static int handle_query(
        int sock,
        const sockaddr* from,
        size_t addrlen,
        mdns_entry_type_t entry,
        std::uint16_t query_id,
        std::uint16_t rtype,
        std::uint16_t rclass,
        std::uint32_t ttl,
        const void* data,
        size_t size,
        size_t name_offset,
        size_t name_length,
        size_t record_offset,
        size_t record_length,
        void* user_data);

    void listen_loop();
    bool announce_current();
    bool goodbye_current();

    std::string service_name_;
    MdnsRuntime* runtime_ = nullptr;
    std::optional<ServiceAnnouncement> current_;
    int socket_ = -1;
    bool running_ = false;
    std::thread listener_thread_;
    mutable std::mutex mutex_;
};

} // namespace tightrope::sync::discovery
