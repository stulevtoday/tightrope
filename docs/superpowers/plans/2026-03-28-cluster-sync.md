# Cluster Sync Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional multi-node synchronization with deterministic convergence while keeping it off the single-node critical path.

**Architecture:** Build the sync stack from the inside out: HLC and journaling first, then CDC and CRDT merge, then transport/protocol, then Raft-backed strong-consistency tables, and only then the bridge/UI controls. Keep the whole feature behind a config flag until parity work is complete.

**Tech Stack:** SQLite, Catch2, LZ4, libsodium/mbedTLS, NuRaft, vendored mDNS, feature flags.

## Status Snapshot (2026-03-28)

- [x] HLC primitives implemented and unit-tested
- [x] Journal append/query/rollback/mark-applied implemented and unit-tested
- [x] Journal compaction guardrail implemented and unit-tested
- [x] CDC trigger SQL generation implemented and unit-tested
- [x] Conflict resolver (LWW) implemented and unit-tested
- [x] Consensus core foundations implemented and unit-tested (`raft_log`, `raft_node`, `raft_rpc`, `membership`, `leader_proxy`)
- [x] Sync protocol/transport/discovery implementation (`sync_protocol`, `sync_engine`, `tls_stream`, `lz4_codec`, `rpc_channel`, `mdns_*`, `peer_manager`)
- [x] CRDT store/sync implementation (`g_counter`, `pn_counter`, `lww_register`, `or_set`, `crdt_store`, `crdt_sync`)
- [x] Bridge-facing cluster controls and feature-flag gating (`clusterEnable/Disable/Status`, peer management, sync trigger/rollback)
- [x] End-to-end raft cluster integration test (3-node election/replication path)

---

## Chunk 1: Local Sync Primitives

### Task 1: Implement HLC And Journal Basics

**Files:**
- Modify: `native/sync/hlc.hpp`
- Modify: `native/sync/hlc.cpp`
- Modify: `native/sync/checksum.hpp`
- Modify: `native/sync/checksum.cpp`
- Modify: `native/sync/journal.hpp`
- Modify: `native/sync/journal.cpp`
- Create: `native/tests/unit/sync/hlc_test.cpp`
- Create: `native/tests/integration/sync/journal_test.cpp`

- [x] **Step 1: Write the failing HLC/journal tests**

```cpp
TEST_CASE("hlc advances monotonically across local events", "[sync][hlc]") {
    HLCClock clock(7);
    auto a = clock.tick();
    auto b = clock.tick();
    REQUIRE(b > a);
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run:
```bash
./build-debug/tightrope-tests "[sync][hlc]"
./build-debug/tightrope-tests "[sync][journal]"
```
Expected: FAIL.

- [x] **Step 3: Implement HLC, checksum, and journal append/read/compact**

Include:

- lexicographic comparison
- SHA-256 entry checksum
- append-only journal rows
- compaction guardrails

- [x] **Step 4: Re-run the HLC/journal tests**

Run:
```bash
./build-debug/tightrope-tests "[sync][hlc]"
./build-debug/tightrope-tests "[sync][journal]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/sync/hlc.hpp native/sync/hlc.cpp native/sync/checksum.hpp native/sync/checksum.cpp native/sync/journal.hpp native/sync/journal.cpp native/tests/unit/sync/hlc_test.cpp native/tests/integration/sync/journal_test.cpp
git commit -m "feat: add sync hlc and journal core"
```

### Task 2: Implement CDC Trigger Generation And Rollback

**Files:**
- Modify: `native/sync/cdc_triggers.hpp`
- Modify: `native/sync/cdc_triggers.cpp`
- Modify: `native/sync/conflict_resolver.hpp`
- Modify: `native/sync/conflict_resolver.cpp`
- Create: `native/tests/integration/sync/cdc_trigger_test.cpp`

- [x] **Step 1: Write the failing CDC test**

```cpp
TEST_CASE("cdc trigger generator emits accounts insert trigger sql", "[sync][cdc]") {
    auto sql = build_insert_trigger("accounts", {"id", "email"});
    REQUIRE(sql.find("_sync_journal") != std::string::npos);
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[sync][cdc]"
```
Expected: FAIL.

- [x] **Step 3: Implement trigger SQL generation and rollback primitives**

Cover:

- insert/update/delete trigger generation
- batch rollback helper
- LWW conflict decision hook

- [x] **Step 4: Re-run the CDC test**

Run:
```bash
./build-debug/tightrope-tests "[sync][cdc]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/sync/cdc_triggers.hpp native/sync/cdc_triggers.cpp native/sync/conflict_resolver.hpp native/sync/conflict_resolver.cpp native/tests/integration/sync/cdc_trigger_test.cpp
git commit -m "feat: add sync cdc and rollback primitives"
```

## Chunk 2: CRDT, Transport, And Consensus

### Task 3: Implement CRDT State Types And Merge Logic

**Files:**
- Modify: `native/sync/crdt/g_counter.hpp`
- Modify: `native/sync/crdt/pn_counter.hpp`
- Modify: `native/sync/crdt/lww_register.hpp`
- Modify: `native/sync/crdt/or_set.hpp`
- Modify: `native/sync/crdt/crdt_store.hpp`
- Modify: `native/sync/crdt/crdt_store.cpp`
- Modify: `native/sync/crdt/crdt_sync.hpp`
- Modify: `native/sync/crdt/crdt_sync.cpp`
- Create: `native/tests/unit/sync/crdt_test.cpp`

- [x] **Step 1: Write the failing CRDT merge test**

```cpp
TEST_CASE("pn counter merge is commutative", "[sync][crdt]") {
    PNCounter a;
    PNCounter b;
    a.add(1, 3);
    b.add(2, 5);
    a.merge(b);
    REQUIRE(a.value() == 8);
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[sync][crdt]"
```
Expected: FAIL.

- [x] **Step 3: Implement the CRDT types and persistence**

Map first:

- `usage_history` → PN-Counter
- `sticky_sessions` → LWW-Register
- `ip_allowlist` → OR-Set

- [x] **Step 4: Re-run the CRDT tests**

Run:
```bash
./build-debug/tightrope-tests "[sync][crdt]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/sync/crdt native/tests/unit/sync/crdt_test.cpp
git commit -m "feat: add crdt merge types"
```

### Task 4: Implement Sync Protocol, Transport, And Peer Discovery

**Files:**
- Modify: `native/sync/sync_protocol.hpp`
- Modify: `native/sync/sync_protocol.cpp`
- Modify: `native/sync/sync_engine.hpp`
- Modify: `native/sync/sync_engine.cpp`
- Modify: `native/sync/transport/tls_stream.hpp`
- Modify: `native/sync/transport/tls_stream.cpp`
- Modify: `native/sync/transport/lz4_codec.hpp`
- Modify: `native/sync/transport/lz4_codec.cpp`
- Modify: `native/sync/transport/rpc_channel.hpp`
- Modify: `native/sync/transport/rpc_channel.cpp`
- Modify: `native/sync/discovery/mdns_publisher.hpp`
- Modify: `native/sync/discovery/mdns_publisher.cpp`
- Modify: `native/sync/discovery/mdns_browser.hpp`
- Modify: `native/sync/discovery/mdns_browser.cpp`
- Modify: `native/sync/discovery/peer_manager.hpp`
- Modify: `native/sync/discovery/peer_manager.cpp`
- Create: `native/tests/integration/sync/sync_protocol_test.cpp`

- [x] **Step 1: Write the failing sync protocol test**

```cpp
TEST_CASE("sync protocol round-trips a journal batch", "[sync][protocol]") {
    auto encoded = encode_batch(sample_journal_batch());
    auto decoded = decode_batch(encoded);
    REQUIRE(decoded.entries.size() == sample_journal_batch().entries.size());
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[sync][protocol]"
```
Expected: FAIL.

- [x] **Step 3: Implement protocol framing, compression, TLS, and mDNS peer tracking**

Preserve:

- handshake shape
- batch compression
- peer high-water marks
- cluster-name isolation

- [x] **Step 4: Re-run the sync protocol test**

Run:
```bash
./build-debug/tightrope-tests "[sync][protocol]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/sync/sync_protocol.hpp native/sync/sync_protocol.cpp native/sync/sync_engine.hpp native/sync/sync_engine.cpp native/sync/transport native/sync/discovery native/tests/integration/sync/sync_protocol_test.cpp
git commit -m "feat: add sync transport and protocol"
```

### Task 5: Implement Raft And Bridge Controls Behind A Feature Flag

Progress note: Raft primitives, bridge controls, and the 3-node integration harness are now implemented and covered by tests.

**Files:**
- Modify: `native/sync/consensus/raft_node.hpp`
- Modify: `native/sync/consensus/raft_node.cpp`
- Modify: `native/sync/consensus/raft_log.hpp`
- Modify: `native/sync/consensus/raft_log.cpp`
- Modify: `native/sync/consensus/raft_rpc.hpp`
- Modify: `native/sync/consensus/raft_rpc.cpp`
- Modify: `native/sync/consensus/membership.hpp`
- Modify: `native/sync/consensus/membership.cpp`
- Modify: `native/sync/consensus/leader_proxy.hpp`
- Modify: `native/sync/consensus/leader_proxy.cpp`
- Modify: `native/bridge/bridge.h`
- Modify: `native/bridge/bridge.cpp`
- Modify: `app/src/main/native.ts`
- Create: `native/tests/integration/sync/raft_cluster_test.cpp`

- [x] **Step 1: Write the failing raft cluster test**

```cpp
TEST_CASE("three-node raft cluster elects one leader", "[sync][raft]") {
    auto cluster = start_test_raft_cluster(3);
    REQUIRE(cluster.leader_count() == 1);
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[sync][raft]"
```
Expected: FAIL.

- [x] **Step 3: Implement minimal Raft and bridge controls behind a flag**

Expose only when `cluster_enabled` is true:

- `clusterEnable`
- `clusterDisable`
- `clusterStatus`
- `clusterAddPeer`
- `clusterRemovePeer`
- `syncTriggerNow`
- `syncRollbackBatch`

- [x] **Step 4: Re-run the raft cluster test**

Run:
```bash
./build-debug/tightrope-tests "[sync][raft]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/sync/consensus native/bridge/bridge.h native/bridge/bridge.cpp app/src/main/native.ts native/tests/integration/sync/raft_cluster_test.cpp
git commit -m "feat: add raft-backed cluster controls"
```
