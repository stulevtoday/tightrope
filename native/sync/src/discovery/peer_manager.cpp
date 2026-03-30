#include "discovery/peer_manager.h"

#include <algorithm>
#include <string>
#include <utility>

#include "sync_logging.h"

namespace tightrope::sync::discovery {

PeerManager::PeerManager(std::string cluster_name)
    : cluster_name_(std::move(cluster_name)), browser_(cluster_name_) {
    log_discovery_event(
        SyncLogLevel::Debug,
        "peer_manager",
        "constructed",
        "cluster=" + cluster_name_);
}

bool PeerManager::add_manual_peer(const std::uint32_t site_id, const PeerEndpoint& endpoint) {
    if (site_id == 0 || !is_valid_endpoint(endpoint)) {
        log_discovery_event(
            SyncLogLevel::Warning,
            "peer_manager",
            "add_manual_peer_rejected",
            "site_id=" + std::to_string(site_id));
        return false;
    }
    manual_peers_[site_id] = {
        .site_id = site_id,
        .endpoint = endpoint,
        .manual = true,
        .seen_unix_ms = 0,
    };
    log_discovery_event(
        SyncLogLevel::Debug,
        "peer_manager",
        "add_manual_peer_applied",
        "site_id=" + std::to_string(site_id) + " endpoint=" + endpoint_to_string(endpoint));
    return true;
}

bool PeerManager::remove_peer(const std::uint32_t site_id) {
    const auto erased = manual_peers_.erase(site_id);
    log_discovery_event(
        SyncLogLevel::Debug,
        "peer_manager",
        "remove_peer",
        "site_id=" + std::to_string(site_id) + " removed=" + std::to_string(erased));
    return erased > 0;
}

bool PeerManager::observe_discovery(const DiscoveredPeer& peer) {
    if (peer.cluster_name != cluster_name_) {
        log_discovery_event(
            SyncLogLevel::Trace,
            "peer_manager",
            "observe_discovery_ignored_cluster",
            "expected=" + cluster_name_ + " actual=" + peer.cluster_name);
        return false;
    }
    const auto ingested = browser_.ingest(peer);
    log_discovery_event(
        SyncLogLevel::Trace,
        "peer_manager",
        "observe_discovery_applied",
        "site_id=" + std::to_string(peer.site_id) + " ingested=" + std::string(ingested ? "1" : "0"));
    return ingested;
}

std::size_t PeerManager::refresh_discovery(const std::uint64_t seen_unix_ms, const int timeout_ms) {
    const auto discovered = browser_.discover_once(seen_unix_ms, timeout_ms);
    log_discovery_event(
        SyncLogLevel::Debug,
        "peer_manager",
        "refresh_discovery",
        "inserted=" + std::to_string(discovered) + " seen_unix_ms=" + std::to_string(seen_unix_ms) +
            " timeout_ms=" + std::to_string(timeout_ms));
    return discovered;
}

void PeerManager::prune_discovered(const std::uint64_t cutoff_unix_ms) {
    const auto before = browser_.peers().size();
    browser_.prune_older_than(cutoff_unix_ms);
    const auto after = browser_.peers().size();
    const auto removed = before >= after ? before - after : 0;
    if (removed > 0) {
        log_discovery_event(
            SyncLogLevel::Debug,
            "peer_manager",
            "prune_discovered",
            "removed=" + std::to_string(removed) + " cutoff_unix_ms=" + std::to_string(cutoff_unix_ms));
    }
}

std::vector<PeerRecord> PeerManager::peers() const {
    boost::container::flat_map<std::uint32_t, PeerRecord> merged;
    for (const auto& peer : browser_.peers()) {
        merged[peer.site_id] = {
            .site_id = peer.site_id,
            .endpoint = peer.endpoint,
            .manual = false,
            .seen_unix_ms = peer.seen_unix_ms,
        };
    }
    for (const auto& [site_id, peer] : manual_peers_) {
        merged[site_id] = peer;
    }

    std::vector<PeerRecord> out;
    out.reserve(merged.size());
    for (const auto& [_, peer] : merged) {
        out.push_back(peer);
    }
    return out;
}

std::vector<PeerRecord> PeerManager::membership_proposals(const std::size_t limit) const {
    auto items = peers();
    std::sort(items.begin(), items.end(), [](const PeerRecord& lhs, const PeerRecord& rhs) {
        if (lhs.manual != rhs.manual) {
            return lhs.manual && !rhs.manual;
        }
        if (lhs.seen_unix_ms != rhs.seen_unix_ms) {
            return lhs.seen_unix_ms > rhs.seen_unix_ms;
        }
        return lhs.site_id < rhs.site_id;
    });

    if (limit > 0 && items.size() > limit) {
        items.resize(limit);
    }
    log_discovery_event(
        SyncLogLevel::Trace,
        "peer_manager",
        "membership_proposals",
        "limit=" + std::to_string(limit) + " count=" + std::to_string(items.size()));
    return items;
}

} // namespace tightrope::sync::discovery
