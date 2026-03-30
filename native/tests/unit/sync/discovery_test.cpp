#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include "discovery/mdns_browser.h"
#include "discovery/mdns_publisher.h"
#include "discovery/peer_endpoint.h"
#include "discovery/peer_manager.h"

TEST_CASE("mdns publisher validates and tracks current announcement", "[sync][discovery]") {
    tightrope::sync::discovery::MdnsPublisher publisher;
    const tightrope::sync::discovery::ServiceAnnouncement valid = {
        .cluster_name = "cluster-a",
        .site_id = 7,
        .endpoint = {.host = "127.0.0.1", .port = 9001},
    };
    const tightrope::sync::discovery::ServiceAnnouncement invalid = {
        .cluster_name = "cluster-a",
        .site_id = 0,
        .endpoint = {.host = "127.0.0.1", .port = 9001},
    };

    REQUIRE_FALSE(publisher.publish(invalid));
    if (!publisher.publish(valid)) {
        SKIP("mDNS socket publish unavailable on this environment");
    }
    REQUIRE(publisher.published());
    REQUIRE(publisher.current().has_value());
    REQUIRE(publisher.current()->site_id == 7);
    REQUIRE(tightrope::sync::discovery::endpoint_to_string(publisher.current()->endpoint) == "127.0.0.1:9001");

    publisher.unpublish();
    REQUIRE_FALSE(publisher.published());
}

TEST_CASE("mdns browser isolates cluster and prunes stale peers", "[sync][discovery]") {
    tightrope::sync::discovery::MdnsBrowser browser("cluster-a");

    REQUIRE(browser.ingest({
        .cluster_name = "cluster-a",
        .site_id = 1,
        .endpoint = {.host = "10.0.0.1", .port = 7777},
        .seen_unix_ms = 100,
    }));
    REQUIRE_FALSE(browser.ingest({
        .cluster_name = "cluster-b",
        .site_id = 2,
        .endpoint = {.host = "10.0.0.2", .port = 7778},
        .seen_unix_ms = 100,
    }));
    REQUIRE_FALSE(browser.ingest({
        .cluster_name = "cluster-a",
        .site_id = 3,
        .endpoint = {.host = "bad host value", .port = 7779},
        .seen_unix_ms = 100,
    }));

    auto peers = browser.peers();
    REQUIRE(peers.size() == 1);
    REQUIRE(peers.front().site_id == 1);

    browser.prune_older_than(101);
    peers = browser.peers();
    REQUIRE(peers.empty());
}

TEST_CASE("peer manager merges manual and discovered peers for membership proposals", "[sync][discovery]") {
    tightrope::sync::discovery::PeerManager manager("cluster-a");

    REQUIRE(manager.add_manual_peer(10, {.host = "manual-node.local", .port = 6001}));
    REQUIRE(manager.observe_discovery({
        .cluster_name = "cluster-a",
        .site_id = 2,
        .endpoint = {.host = "10.1.0.2", .port = 6002},
        .seen_unix_ms = 110,
    }));
    REQUIRE(manager.observe_discovery({
        .cluster_name = "cluster-a",
        .site_id = 10,
        .endpoint = {.host = "10.1.0.10", .port = 6010},
        .seen_unix_ms = 120,
    }));

    auto peers = manager.peers();
    REQUIRE(peers.size() == 2);

    const auto by_site_10 =
        std::find_if(peers.begin(), peers.end(), [](const auto& peer) { return peer.site_id == 10; });
    REQUIRE(by_site_10 != peers.end());
    REQUIRE(by_site_10->manual);
    REQUIRE(by_site_10->endpoint.host == "manual-node.local");

    const auto proposals = manager.membership_proposals();
    REQUIRE(proposals.size() == 2);
    REQUIRE(proposals.front().site_id == 10);
    REQUIRE(proposals.front().manual);
    REQUIRE(proposals.back().site_id == 2);
}

TEST_CASE("mdns publisher and browser discover live service announcement", "[sync][discovery][mdns]") {
    static std::atomic<std::uint16_t> port_seed{39100};

    const auto sync_port = static_cast<std::uint16_t>(port_seed.fetch_add(1));
    const auto site_id = static_cast<std::uint32_t>(sync_port);
    const std::uint64_t seen_unix_ms = 5000;

    tightrope::sync::discovery::MdnsPublisher publisher;
    const bool published = publisher.publish({
        .cluster_name = "cluster-live-mdns",
        .site_id = site_id,
        .endpoint = {.host = "127.0.0.1", .port = sync_port},
    });
    if (!published) {
        SKIP("mDNS socket publish unavailable on this environment");
    }

    tightrope::sync::discovery::MdnsBrowser browser("cluster-live-mdns");

    bool found = false;
    for (int attempt = 0; attempt < 12; ++attempt) {
        (void)browser.discover_once(seen_unix_ms + static_cast<std::uint64_t>(attempt), 180);
        const auto discovered = browser.peers();
        const auto it = std::find_if(discovered.begin(), discovered.end(), [site_id](const auto& peer) {
            return peer.site_id == site_id;
        });
        if (it != discovered.end()) {
            found = true;
            REQUIRE(it->cluster_name == "cluster-live-mdns");
            REQUIRE(it->endpoint.port == sync_port);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    publisher.unpublish();
    REQUIRE(found);
}

TEST_CASE("peer manager network refresh keeps cluster isolation", "[sync][discovery][mdns]") {
    static std::atomic<std::uint16_t> port_seed{40200};

    const auto sync_port = static_cast<std::uint16_t>(port_seed.fetch_add(1));
    const auto site_id = static_cast<std::uint32_t>(sync_port);

    tightrope::sync::discovery::MdnsPublisher publisher;
    const bool published = publisher.publish({
        .cluster_name = "cluster-source-mdns",
        .site_id = site_id,
        .endpoint = {.host = "127.0.0.1", .port = sync_port},
    });
    if (!published) {
        SKIP("mDNS socket publish unavailable on this environment");
    }

    tightrope::sync::discovery::PeerManager same_cluster("cluster-source-mdns");
    tightrope::sync::discovery::PeerManager other_cluster("cluster-target-mdns");

    bool same_cluster_seen = false;
    for (int attempt = 0; attempt < 12; ++attempt) {
        (void)same_cluster.refresh_discovery(7000 + static_cast<std::uint64_t>(attempt), 180);
        (void)other_cluster.refresh_discovery(7000 + static_cast<std::uint64_t>(attempt), 180);

        const auto same_peers = same_cluster.peers();
        if (std::any_of(same_peers.begin(), same_peers.end(), [site_id](const auto& peer) { return peer.site_id == site_id; })) {
            same_cluster_seen = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    publisher.unpublish();

    REQUIRE(same_cluster_seen);
    const auto other_peers = other_cluster.peers();
    const auto leaked = std::find_if(other_peers.begin(), other_peers.end(), [site_id](const auto& peer) {
        return peer.site_id == site_id;
    });
    REQUIRE(leaked == other_peers.end());
}
