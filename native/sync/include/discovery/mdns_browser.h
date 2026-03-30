#pragma once
// Browse and resolve peer services

#include <cstdint>
#include <string>
#include <vector>

#include <boost/container/flat_map.hpp>

#include "mdns.h"

#include "peer_endpoint.h"

namespace tightrope::sync::discovery {

class MdnsRuntime;

struct DiscoveredPeer {
    std::string cluster_name;
    std::uint32_t site_id = 0;
    PeerEndpoint endpoint;
    std::uint64_t seen_unix_ms = 0;
};

class MdnsBrowser {
public:
    explicit MdnsBrowser(
        std::string cluster_name,
        std::string service_name = "_tightrope-sync._tcp",
        MdnsRuntime* runtime = nullptr);
    ~MdnsBrowser();

    bool ingest(const DiscoveredPeer& peer);
    std::size_t discover_once(std::uint64_t seen_unix_ms, int timeout_ms = 120);
    void stop();
    void prune_older_than(std::uint64_t cutoff_unix_ms);
    void clear();

    std::vector<DiscoveredPeer> peers() const;
    const std::string& cluster_name() const;

private:
    static int handle_record(
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

    bool ensure_socket();

    std::string cluster_name_;
    std::string service_name_;
    MdnsRuntime* runtime_ = nullptr;
    int socket_ = -1;
    boost::container::flat_map<std::uint32_t, DiscoveredPeer> peers_;
};

} // namespace tightrope::sync::discovery
