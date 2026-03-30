#pragma once
// Track discovered + manual peers, propose membership

#include <cstdint>
#include <string>
#include <vector>

#include <boost/container/flat_map.hpp>

#include "mdns_browser.h"

namespace tightrope::sync::discovery {

struct PeerRecord {
    std::uint32_t site_id = 0;
    PeerEndpoint endpoint;
    bool manual = false;
    std::uint64_t seen_unix_ms = 0;
};

class PeerManager {
public:
    explicit PeerManager(std::string cluster_name);

    bool add_manual_peer(std::uint32_t site_id, const PeerEndpoint& endpoint);
    bool remove_peer(std::uint32_t site_id);
    bool observe_discovery(const DiscoveredPeer& peer);
    std::size_t refresh_discovery(std::uint64_t seen_unix_ms, int timeout_ms = 120);
    void prune_discovered(std::uint64_t cutoff_unix_ms);

    std::vector<PeerRecord> peers() const;
    std::vector<PeerRecord> membership_proposals(std::size_t limit = 0) const;

private:
    std::string cluster_name_;
    MdnsBrowser browser_;
    boost::container::flat_map<std::uint32_t, PeerRecord> manual_peers_;
};

} // namespace tightrope::sync::discovery
