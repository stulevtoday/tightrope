# Tightrope C++ Fix Guide: Senior Engineer Audit

**Date:** 2026-03-31
**Verdict:** This codebase is 60% production-ready. The proxy/balancer/auth layers are solid. The consensus/sync layer is a thin NuRaft wrapper with critical bugs that will lose data on restart. The bridge works but swallows error context. Multiple stub files ship zero functionality. Below is every issue, ranked by blast radius.

---

## Table of Contents

1. [SEVERITY P0 -- Data Loss / Crash Recovery Failures](#p0---data-loss--crash-recovery-failures)
2. [SEVERITY P1 -- Stub Files Shipping Zero Code](#p1---stub-files-shipping-zero-code)
3. [SEVERITY P2 -- Thread Safety Violations](#p2---thread-safety-violations)
4. [SEVERITY P3 -- Bridge / Frontend-Backend Gaps](#p3---bridge--frontend-backend-gaps)
5. [SEVERITY P4 -- Consensus Protocol Gaps](#p4---consensus-protocol-gaps)
6. [SEVERITY P5 -- Journaling / WAL Gaps](#p5---journaling--wal-gaps)
7. [SEVERITY P6 -- Replication / Networking Gaps](#p6---replication--networking-gaps)
8. [SEVERITY P7 -- Memory Management & Resource Leaks](#p7---memory-management--resource-leaks)
9. [SEVERITY P8 -- Error Handling & Validation](#p8---error-handling--validation)
10. [SEVERITY P9 -- Hardcoded Config & Missing Tunables](#p9---hardcoded-config--missing-tunables)
11. [SEVERITY P10 -- Test Coverage Gaps](#p10---test-coverage-gaps)
12. [Summary Scorecard](#summary-scorecard)

---

## P0 - Data Loss / Crash Recovery Failures

### P0 Implementation Status (2026-03-31)

| Item | Status | Implementation | Test Coverage |
|---|---|---|---|
| **P0-1** deterministic storage path | Done | `native/sync/src/consensus/nuraft_backend_common.cpp` | `native/tests/unit/sync/raft_node_test.cpp` (`[sync][raft][node][p0]`) |
| **P0-2** state machine persistence | Done | `native/sync/src/consensus/nuraft_backend_state.cpp`, `native/sync/src/consensus/nuraft_backend_storage.cpp` | `native/tests/unit/sync/raft_node_test.cpp` (`[sync][raft][node][p0]`) |
| **P0-3** journal durability PRAGMAs | Done | `native/sync/src/sync_schema.cpp` | `native/tests/integration/sync/sync_schema_test.cpp` (`[sync][schema][integration][p0]`) |
| **P0-4** durable checkpoint mode (`TRUNCATE`) | Done | `native/sync/src/consensus/nuraft_backend_storage.cpp` | `native/tests/unit/sync/nuraft_backend_storage_test.cpp` (`[sync][raft][storage][p0]`) |

**Build verification note (Windows):** `tightrope-core` builds successfully after restoring the missing `SqliteRaftStorage` forward declaration in `native/sync/include/consensus/internal/nuraft_backend_components.h`. The aggregate `tightrope-tests` target still fails on existing POSIX-only tests (`setenv`, `unsetenv`, `unistd.h`, `arpa/inet.h`) that are unrelated to this P0 work.

### P0-1: Raft Storage Path is Non-Deterministic -- DATA LOSS ON RESTART

**File:** `native/sync/src/consensus/nuraft_backend_common.cpp:56-71`

```cpp
std::string make_storage_path(const std::uint32_t node_id, const std::uint16_t port_base) {
    static std::atomic<std::uint64_t> sequence{1};  // <-- INCREMENTS EVERY CALL
    // ...
    const auto run_id = sequence.fetch_add(1);
    const auto filename = "nuraft-" + std::to_string(port_base) + "-" +
                          std::to_string(node_id) + "-" + std::to_string(run_id) + ".db";
    return (root / filename).string();
}
```

**Impact:** Every time a `Backend` is constructed, it gets a NEW database file. All previous Raft log entries, committed state, term, and vote records are **permanently orphaned**. The node restarts as if it never existed. This is a **total data loss bug** masked by short-lived tests.

**Fix:** Storage path must be deterministic based on `(cluster_name, node_id)`. Remove the atomic sequence counter entirely. Pass the path from the `BridgeConfig` or derive it from `db_path`:

```cpp
std::string make_storage_path(const std::string& base_dir, std::uint32_t node_id) {
    auto root = std::filesystem::path(base_dir) / "raft";
    std::filesystem::create_directories(root);
    return (root / ("raft-node-" + std::to_string(node_id) + ".db")).string();
}
```

---

### P0-2: State Machine is In-Memory Only -- COMMITTED STATE LOST ON CRASH

**File:** `native/sync/src/consensus/nuraft_backend_state.cpp:19-26`

```cpp
nuraft::ptr<nuraft::buffer> InMemoryStateMachine::commit(const nuraft::ulong log_idx, nuraft::buffer& data) {
    // ...
    committed_payloads_.emplace_back(reinterpret_cast<const char*>(bytes), size);
    commit_index_.store(log_idx);
    return nullptr;
}
```

`committed_payloads_` is `std::vector<std::string>` -- pure heap memory. When the process exits, all applied entries evaporate. After restart + log replay, the state machine will re-apply entries, but only if the log still exists (see P0-1 above -- it won't).

**Fix:** The state machine must either:
1. Apply entries directly to SQLite (the application database), or
2. Persist its state to a durable snapshot file that survives restart.

Option 1 is correct for this architecture -- committed Raft payloads should be applied to the main `store.db` as SQL mutations.

---

### P0-3: Main Journal Database Lacks Durability PRAGMAs

**Files:** `native/sync/src/persistent_journal.cpp`, `native/sync/src/sync_schema.cpp`

The Raft storage correctly sets `PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;` (in `nuraft_backend_storage.cpp:39`). But the main `_sync_journal` table lives on a **different** `sqlite3*` connection (`db_` in PersistentJournal) that **never** sets these PRAGMAs.

**Impact:** SQLite defaults to DELETE journal mode with `synchronous=FULL`, which is safe but slow. If the connection is opened with `synchronous=NORMAL` (a common optimization), journal entries can be lost on power failure. This is an implicit dependency on SQLite defaults that must be made explicit.

**Fix:** In `sync_schema.cpp::ensure_sync_schema()` or wherever the main DB connection is opened, add:

```cpp
exec_sql(db, "PRAGMA journal_mode=WAL;");
exec_sql(db, "PRAGMA synchronous=FULL;");
```

---

### P0-4: `flush()` Uses PASSIVE Checkpoint -- Not Durable

**File:** `native/sync/src/consensus/nuraft_backend_storage.cpp:237-240`

```cpp
bool SqliteRaftStorage::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    return exec_locked("PRAGMA wal_checkpoint(PASSIVE);");
}
```

`PASSIVE` is non-blocking and may not checkpoint all frames. A crash between commit and checkpoint loses data in WAL frames.

**Fix:** Use `TRUNCATE` or `RESTART` for explicit flush calls:

```cpp
return exec_locked("PRAGMA wal_checkpoint(TRUNCATE);");
```

---

## P1 - Stub Files Shipping Zero Code

These files declare a namespace, have a comment, and contain **nothing**. Any code that includes these headers gets a false sense of functionality.

| Header File | Matching .cpp | What It Claims To Be |
|---|---|---|
| `native/auth/crypto/include/crypto/fernet.h` | `native/auth/crypto/src/fernet.cpp` | Fernet-compatible encrypt/decrypt |
| `native/auth/crypto/include/crypto/key_file.h` | `native/auth/crypto/src/key_file.cpp` | Encryption key file I/O |
| `native/core/json/include/json/serialize.h` | `native/core/json/src/serialize.cpp` | JSON serialization templates |
| `native/core/time/include/time/clock.h` | (no .cpp) | Mockable clock abstraction |
| `native/core/time/include/time/ewma.h` | (no .cpp) | EWMA template |
| `native/usage/include/cost_calculator.h` | `native/usage/src/cost_calculator.cpp` | Per-request cost estimation |
| `native/bridge/include/bridge_helpers.h` | (no .cpp) | N-API <-> C++ type conversions |

**Impact per stub:**

- **fernet.h / key_file.h**: No encryption at rest. API keys, OAuth tokens, and session data are stored in plaintext SQLite. A Fortune 500 company storing credentials unencrypted is a compliance violation (SOC2, PCI-DSS, HIPAA).
- **serialize.h**: The codebase uses `glaze` for JSON, so this may be intentionally deferred. But the file exists and lies about its contents.
- **clock.h**: Without a mockable clock, all time-dependent tests use real `steady_clock` / `system_clock`. This makes tests non-deterministic and prevents testing time-sensitive edge cases (token expiry, session TTL, HLC drift).
- **ewma.h**: The balancer has `latency_ewma.h` strategy -- if this EWMA template is empty, the latency-weighted strategy may be using an inline implementation. Verify.
- **cost_calculator.h**: Cost-aware balancing (`cost_aware.h`) exists but per-request cost estimation is empty. Cost calculations are either hardcoded elsewhere or non-functional.
- **bridge_helpers.h**: Type conversion helpers are actually in `addon_bindings_support.h/cpp`. This file is dead.

**Fix:** Either implement these or delete them. Empty files that pretend to be modules are worse than no files at all. For Fortune 500 tier:
1. **Implement fernet.h** with libsodium `crypto_secretbox` (not actual Fernet, which uses AES-CBC -- use XSalsa20-Poly1305).
2. **Implement key_file.h** with secure key derivation (argon2id via `crypto_pwhash`).
3. **Delete** serialize.h, bridge_helpers.h if genuinely unused.
4. **Implement clock.h** as a virtual clock interface for testability.

---

## P2 - Thread Safety Violations

### P2-1: `DashboardSessionManager` -- Unprotected Concurrent Map Access

**File:** `native/auth/dashboard/include/dashboard/session_manager.h:28-29`

```cpp
private:
    std::int64_t ttl_ms_ = 12 * 60 * 60 * 1000;
    std::unordered_map<std::string, DashboardSessionState> sessions_;
```

`create()`, `get()`, `erase()`, and `purge_expired()` all touch `sessions_` with **zero synchronization**. Multiple HTTP request threads calling these concurrently will corrupt the map, crash with iterator invalidation, or produce undefined behavior.

**Fix:** Add `mutable std::mutex mutex_;` and lock in every public method, or use a `std::shared_mutex` for read-heavy workloads (get vs create/erase).

---

### P2-2: `WeightedRoundRobinPicker` -- Unprotected Mutable State

**File:** `native/balancer/strategies/include/strategies/weighted_round_robin.h:20`

```cpp
private:
    std::unordered_map<std::string, double> current_weights_;
```

`pick()` modifies `current_weights_` on every call. If the load balancer is called from multiple request-handling threads (which it will be), this is a data race.

**Fix:** Either make the picker per-thread, or add a mutex.

---

### P2-3: RNG in Balancer Strategies -- Not Thread-Safe

**Files:**
- `native/balancer/strategies/src/success_rate.cpp` -- `std::mt19937 rng_;`
- `native/balancer/strategies/src/power_of_two.cpp` -- `std::mt19937 rng_;`

`std::mt19937` is not thread-safe. Concurrent calls to `pick()` from different request threads will corrupt the RNG state.

**Fix:** Use `thread_local std::mt19937` or protect with a mutex. Thread-local is preferred for performance:

```cpp
thread_local std::mt19937 rng_{std::random_device{}()};
```

---

## P3 - Bridge / Frontend-Backend Gaps

### P3-1: Generic Error Messages Lose All Context

**File:** `native/bridge/src/addon.cpp:92-113`

Every `queue_async_void` call uses a generic error string like `"clusterEnable failed"`, `"bridge init failed"`, etc. When the C++ `Bridge` method returns `false`, the JS side gets zero detail about **why**.

```cpp
// Current: user sees "clusterEnable failed"
// Should see: "clusterEnable failed: port 26001 already in use (EADDRINUSE)"
```

**Fix:** Change `Bridge` methods from `bool` returns to `std::expected<void, std::string>` or at minimum provide a `last_error()` method. Propagate the actual error message through the async worker.

---

### P3-2: Health Status Never Reports "degraded"

**File:** `native/bridge/src/addon.cpp:180`

```cpp
object.Set("status", snapshot.running ? "ok" : "error");
```

The preload TypeScript declares `'ok' | 'degraded' | 'error'` but C++ only emits `"ok"` or `"error"`. There is no degraded state (e.g., cluster enabled but no quorum, high error rate, approaching usage limits).

**Fix:** Add degraded detection:

```cpp
if (!snapshot.running) return "error";
if (cluster_enabled && !has_quorum) return "degraded";
return "ok";
```

---

### P3-3: No Async Operation Timeouts

**File:** `native/bridge/src/addon.cpp:19-63`

`AsyncBridgeWorker` has no timeout mechanism. If a native operation hangs (e.g., Raft election that never completes, network call that blocks indefinitely), the JS Promise never resolves. The UI freezes or the operation silently disappears.

**Fix:** Add a deadline to async workers. Use `std::condition_variable::wait_for` or a separate watchdog timer. Cancel the operation and reject the Promise after timeout.

---

### P3-4: No Input Validation in Preload Layer

**File:** `app/src/preload/index.ts`

- `addFirewallIp(ipAddress: string)` -- no IP format validation
- `addPeer(address: string)` -- no `host:port` format validation
- `removePeer(siteId: string)` -- no format validation

Invalid input passes through IPC, through the bridge, and into C++ before being caught (if at all).

**Fix:** Validate at the boundary:

```typescript
addFirewallIp: (ip: string) => {
    if (!/^\d{1,3}(\.\d{1,3}){3}(\/\d{1,2})?$/.test(ip)) {
        return Promise.reject(new Error('Invalid IP format'));
    }
    return ipcRenderer.invoke('firewall:add', ip);
}
```

---

### P3-5: Missing Progress Reporting / Cancellation for Long Operations

The bridge has no mechanism for:
- Reporting sync progress (% of journal entries replicated)
- Cancelling a long-running sync or cluster join
- Streaming cluster state changes to the UI

**Fix:** Add an IPC event channel (`ipcMain.emit` / `ipcRenderer.on`) for push-based status updates from native code.

---

## P4 - Consensus Protocol Gaps

### P4-1: `test_mode_flag_` is TRUE in Production

**File:** `native/sync/src/consensus/nuraft_backend.cpp:105`

```cpp
init_options.test_mode_flag_ = true;
```

NuRaft's test mode relaxes safety checks. This must be `false` in production.

**Fix:** Make this configurable, default to `false`.

---

### P4-2: Membership Changes Not Replicated Through Raft

**File:** `native/sync/src/consensus/membership.cpp`

`begin_joint_consensus()` and `commit_joint_consensus()` modify local state only. They do **not** submit membership changes through NuRaft's configuration change API. A follower could locally commit a config change that the leader has not agreed to.

This violates Raft safety: membership changes must go through the log.

**Fix:** Use NuRaft's `raft_server::add_srv()` and `raft_server::remove_srv()` APIs for membership changes. The local `Membership` class should be a read-only view of the NuRaft cluster config.

---

### P4-3: Leader Proxy Has No Forwarding Implementation

**File:** `native/sync/src/consensus/leader_proxy.cpp`

`decide()` returns `LeaderProxyAction::ForwardToLeader` but there is **no RPC client code** to actually forward the request. The caller must implement forwarding, but no evidence of this exists.

**Fix:** Implement an RPC client that forwards proposals to the current leader, with timeout and retry logic.

---

### P4-4: No Read Linearizability

Reads go directly to the application database without going through the leader. A follower can return stale data.

**Fix:** For strongly-consistent tables (`RaftLinearizable` strategy), implement ReadIndex or lease-based reads through the Raft leader.

---

### P4-5: Dead Code in Consensus Module

These files are **never called in production** because NuRaft handles the logic internally:

| File | What It Is | Status |
|---|---|---|
| `native/sync/src/consensus/raft_log.cpp` | In-memory Raft log | Test-only, dead in production |
| `native/sync/src/consensus/raft_rpc.cpp` | Vote/append RPC logic | Duplicate of NuRaft internals |
| `native/sync/src/consensus/raft_scheduler.cpp` | Election/heartbeat timers | Not used by `RaftNode` |

**Fix:** Move to a `test/` directory or delete. Dead code in `src/` is a maintenance trap.

---

## P5 - Journaling / WAL Gaps

### P5-1: No Automatic Journal Compaction

**File:** `native/sync/src/persistent_journal.cpp:334-378`

`compact()` exists but is never called automatically. The `_sync_journal` table grows unbounded. On a high-throughput node, this will consume all disk space.

**Fix:** Add a background compaction timer:
- Trigger when journal exceeds N entries or M megabytes
- Compact entries older than T seconds AND acknowledged by all peers
- Run `PRAGMA incremental_vacuum` or periodic `VACUUM` to reclaim space

---

### P5-2: No VACUUM After Compaction

SQLite `DELETE` does not reclaim disk space. After `compact()` deletes thousands of rows, the database file stays the same size.

**Fix:** After compaction, run:

```cpp
exec_sql(db, "PRAGMA incremental_vacuum;"); // if auto_vacuum=INCREMENTAL is set
// OR periodically:
exec_sql(db, "VACUUM;"); // expensive, do off-peak
```

---

### P5-3: Checksums Not Verified on Local Read

`journal_checksum()` is computed on append and verified on remote apply (in `sync_engine.cpp:268`), but **never verified when reading local entries**. Silent corruption in the local database goes undetected until a peer tries to apply it and rejects it.

**Fix:** Add optional checksum verification in `PersistentJournal::entries_after()`.

---

### P5-4: No Crash Recovery for Main Journal

There is no replay mechanism for the `_sync_journal` table. If the database is corrupted (torn write in non-WAL mode, disk error), there is no way to recover.

**Fix:**
1. Ensure WAL mode is set (P0-3 above)
2. Run `PRAGMA integrity_check` on startup
3. If corruption detected, rebuild from peer snapshot

---

## P6 - Replication / Networking Gaps

### P6-1: No Authentication -- Any Peer Can Join

**File:** `native/sync/include/transport/tls_stream.h`

The handshake protocol (`sync_protocol.h`) contains `site_id`, `schema_version`, and `last_recv_seq_from_peer` but **no authentication token or certificate verification**. Any device on the network that discovers the mDNS service can join the cluster and inject data.

**Fix:** Add HMAC-SHA256 authentication in the handshake using a pre-shared cluster secret:

```cpp
struct HandshakeFrame {
    std::uint32_t site_id;
    std::uint32_t schema_version;
    std::uint64_t last_recv_seq_from_peer;
    std::array<uint8_t, 32> hmac;  // HMAC-SHA256 over (site_id || schema_version || nonce)
};
```

---

### P6-2: TLS Context Not Initialized

**File:** `native/sync/include/transport/tls_stream.h`

The `TlsStream` class wraps a TCP socket but there is no visible TLS context initialization, certificate loading, or peer verification. It may be a raw TCP connection masquerading as TLS.

**Fix:** Initialize SSL context with certificates, enable peer verification, pin certificates for known peers.

---

### P6-3: No Rate Limiting or Backpressure

No rate limiting exists anywhere in the replication path. A malicious or malfunctioning peer could flood the node with journal batches.

**Fix:** Add:
- Token bucket rate limiter per peer
- Max in-flight batch count
- Connection-level backpressure (stop reading when apply queue is full)

---

### P6-4: No Dead Peer Eviction

When a peer goes permanently offline, it stays in the membership forever. The leader continues trying to replicate to it, wasting resources and potentially blocking quorum if enough peers are dead.

**Fix:** Implement a dead-peer detector:
- Track consecutive heartbeat failures per peer
- After N failures (configurable, e.g., 100), trigger automatic removal via Raft membership change
- Log the eviction prominently

---

### P6-5: No Replication Lag Monitoring

There are no metrics for:
- Per-peer replication lag (leader commit index - follower match index)
- Journal sync latency
- Batch apply duration

**Fix:** Add an observable metrics interface. At minimum, expose lag in `ClusterStatus::peers[].match_index` (already in the struct) and add a `replication_lag_entries` field.

---

### P6-6: No Protocol Version Evolution

**File:** `native/sync/src/sync_protocol.cpp`

`schema_version` is exchanged in the handshake but there is no logic to handle version mismatches. If nodes run different protocol versions, behavior is undefined.

**Fix:** On version mismatch, either negotiate down to the lowest common version or reject the connection with a clear error.

---

## P7 - Memory Management & Resource Leaks

### P7-1: Intentional Static Leaks (18 instances)

The codebase uses a pattern of `static auto* x = new T()` to avoid static destruction order issues. While this is a known C++ idiom, it creates 18 permanent leaks:

| File | Count | What's Leaked |
|---|---|---|
| `native/core/logging/src/logger.cpp` | 2 | LogObserver, mutex |
| `native/auth/oauth/src/oauth_service.cpp` | 4 | mutex, OAuthState, ProviderClient, jthread |
| `native/db/connection/src/sqlite_registry.cpp` | 2 | mutex, unordered_map |
| `native/proxy/src/account_traffic.cpp` | 3 | mutex, unordered_map, callback |
| `native/proxy/session/src/http_bridge.cpp` | 1 | mutex |
| `native/usage/src/usage_fetcher.cpp` | 4 | validator, fetcher, mutex, cache |
| `native/auth/oauth/src/token_refresh.cpp` | 1 | ProviderClient |

**Impact:** These never get freed, but they're process-lifetime objects so the OS reclaims them. The real issue is that asan/msan/valgrind will report them as leaks, making it harder to find real leaks.

**Fix:** Use Meyers singletons or `absl::NoDestructor` to suppress leak warnings while maintaining the same lifetime guarantees:

```cpp
static auto& mutex() {
    static std::mutex instance;
    return instance;
}
```

Or if destruction order truly matters, use `absl::NoDestructor<std::mutex>`.

---

### P7-2: Raw `delete` in Backend Destructor

**File:** `native/sync/src/consensus/nuraft_backend.cpp:55-58`

```cpp
Backend::~Backend() {
    stop();
    delete impl_;
    impl_ = nullptr;
}
```

And in the constructor:
```cpp
Backend::Backend(...) : impl_(new Impl(...)) {}
```

Raw `new`/`delete` is error-prone. If `stop()` throws (it won't due to noexcept, but the pattern is fragile), `impl_` leaks.

**Fix:** Use `std::unique_ptr<Impl>`:

```cpp
class Backend {
    std::unique_ptr<Impl> impl_;
};
```

---

## P8 - Error Handling & Validation

### P8-1: Silently Ignored Database Return Values

**File:** `native/db/repositories/src/api_key_repo_limits.cpp:48,65`

```cpp
(void)delete_stmt.exec();  // Return value explicitly discarded
(void)insert_stmt.exec();  // Return value explicitly discarded
```

If these database operations fail, the code continues as if nothing happened. Data may be inconsistent.

**Fix:** Check return values. Log failures. Propagate errors to callers.

---

### P8-2: Unsafe `reinterpret_cast` for SQLite Text

**Files:**
- `native/auth/oauth/src/token_refresh.cpp:68,71`
- `native/sync/src/persistent_journal.cpp:17`

```cpp
record.refresh_token = reinterpret_cast<const char*>(refresh_raw);
```

`sqlite3_column_text()` returns `const unsigned char*`. A `reinterpret_cast` to `const char*` works on all practical platforms but is technically implementation-defined behavior.

**Fix:** Use a safe conversion wrapper:

```cpp
inline std::string sqlite_text_to_string(const unsigned char* text) {
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}
```

(The `persistent_journal.cpp` helper `column_text()` already does this correctly -- use it everywhere.)

---

### P8-3: Rollback Failure Silently Ignored

**File:** `native/sync/src/consensus/nuraft_backend_storage.cpp:113,119,123,231`

```cpp
(void)exec_locked("ROLLBACK;");
```

If a ROLLBACK fails (e.g., because the connection is broken), the database may be in an inconsistent state. The `(void)` cast explicitly suppresses the warning.

**Fix:** At minimum, log the rollback failure. Consider closing and reopening the connection if rollback fails.

---

### P8-4: No Password Hash Null-Termination Validation

**File:** `native/auth/dashboard/src/password_auth.cpp:38-42`

```cpp
return crypto_pwhash_str_verify(
    std::string(password_hash).c_str(),  // creates temporary string just for c_str()
    password.data(),
    static_cast<unsigned long long>(password.size())
) == 0;
```

Constructing a temporary `std::string` from `string_view` just to get `c_str()` is wasteful. More importantly, if `password_hash` is not null-terminated (it's a `string_view`), this creates a copy to ensure null termination -- which is correct but should be documented.

**Fix:** Assert or validate that the hash has the expected argon2 format before passing to libsodium.

---

## P9 - Hardcoded Config & Missing Tunables

### P9-1: Raft Timeouts Not Configurable

**File:** `native/sync/src/consensus/nuraft_backend.cpp:92-97`

```cpp
params.with_election_timeout_lower(150)
    .with_election_timeout_upper(350)
    .with_hb_interval(75)
    .with_rpc_failure_backoff(50)
    .with_max_append_size(64);
```

These values are reasonable for LAN but wrong for WAN. No way to tune them without recompiling.

**Fix:** Accept these as part of `ClusterConfig`.

---

### P9-2: Sticky Session TTL Hardcoded

**File:** `native/proxy/session/src/sticky_affinity.cpp:42-43`

```cpp
constexpr std::int64_t kStickyTtlMs = 30 * 60 * 1000;      // 30 minutes
constexpr std::int64_t kCleanupIntervalMs = 60 * 1000;       // 1 minute
```

**Fix:** Move to `Config` struct or settings database.

---

### P9-3: NuRaft Thread Pool Size Hardcoded

**File:** `native/sync/src/consensus/nuraft_backend.cpp:100`

```cpp
asio_options.thread_pool_size_ = 2;
```

On a 64-core machine this wastes resources. On a 1-core machine this oversubscribes.

**Fix:** Default to `std::min(std::thread::hardware_concurrency(), 4u)` or make configurable.

---

### P9-4: Default Bind Address is Localhost Only

**File:** `native/bridge/include/bridge.h:19`

```cpp
std::string host = "127.0.0.1";
```

For cluster mode to work, nodes need to bind to a routable address. The default of `127.0.0.1` means cluster mode cannot work without explicit config override.

**Fix:** When cluster mode is enabled, validate that `host` is not localhost, or auto-detect a routable address.

---

## P10 - Test Coverage Gaps

### Missing Test Categories

| Category | Current Coverage | What's Missing |
|---|---|---|
| **Raft failure scenarios** | 0 tests | Leader crash + recovery, follower crash, split-brain simulation |
| **Network partition** | 0 tests | Minority/majority partition behavior, healing |
| **Concurrent operations** | 0 tests | Parallel proposals, concurrent reads/writes, session manager thread safety |
| **Crash recovery** | 0 tests | Kill process mid-write, restart, verify data integrity |
| **Full disk handling** | 0 tests | SQLite IOERR behavior, graceful degradation |
| **Protocol version mismatch** | 0 tests | What happens when nodes disagree on schema_version |
| **Security** | 0 tests | Unauthorized peer connection, malformed handshake, TLS verification |
| **Stress / load** | 0 tests | Large log sizes, slow followers, high message loss |
| **Bridge error paths** | 0 tests | Native module failure propagation to JS |
| **Clock.h / time mocking** | Blocked | clock.h is a stub, time-dependent tests use real time |

### Existing Tests That Pass But Prove Nothing

- `raft_node_test.cpp` uses P0-1's non-deterministic storage paths, so "persistence" test is actually testing fresh-start behavior
- `membership_test.cpp` only tests local state, not replicated config changes
- `leader_proxy_test.cpp` tests decision logic but not actual forwarding

---

## Summary Scorecard

| Module | Grade | Blocking Issues | Notes |
|---|---|---|---|
| **Proxy/Streaming** | B+ | None | Solid, well-structured |
| **Balancer** | B | P2-2, P2-3 | Thread safety in pickers |
| **Auth (Dashboard/OAuth)** | B- | P2-1 | Session manager needs mutex |
| **Auth (Crypto)** | F | P1 stubs | No encryption at rest |
| **Database/Repositories** | B | P8-1 | Ignored return values |
| **Bridge (N-API)** | C+ | P3-1, P3-2, P3-3 | Swallows errors, no timeouts |
| **Consensus (Raft)** | D | P0-1, P0-2, P4-1, P4-2 | Data loss on restart, test mode in prod |
| **Journaling** | C | P0-3, P5-1, P5-2 | No durability pragmas, no compaction |
| **Replication** | C- | P6-1, P6-2, P6-3 | No auth, unclear TLS, no rate limiting |
| **Build System** | B+ | None | Well-organized, cross-platform |
| **Test Coverage** | D+ | P10 | Happy path only, no failure scenarios |

### Priority Order for Fixes

1. **P0-1** (storage path) -- Without this fix, nothing in cluster mode works across restarts
2. **P0-2** (state machine persistence) -- Without this, committed data is volatile
3. **P0-3** (journal durability) -- Protect against power loss
4. **P4-1** (test mode flag) -- One-line fix, massive safety improvement
5. **P2-1** (session manager mutex) -- Active crash risk in production
6. **P2-2, P2-3** (balancer thread safety) -- Active data race in hot path
7. **P3-1** (error propagation) -- Users can't debug failures
8. **P6-1** (authentication) -- Cluster is open to any peer on the network
9. **P1** (stubs) -- Delete or implement, stop lying about capabilities
10. **P5-1** (compaction) -- Prevents disk exhaustion over time

### Lines of Code Estimate for Full Fix

| Priority | Estimated LOC | Effort |
|---|---|---|
| P0 fixes | ~200 | 1-2 days |
| P1 stubs (implement crypto) | ~500 | 2-3 days |
| P2 thread safety | ~50 | 2-4 hours |
| P3 bridge improvements | ~300 | 1-2 days |
| P4 consensus fixes | ~400 | 2-3 days |
| P5 journaling fixes | ~150 | 1 day |
| P6 replication/security | ~600 | 3-4 days |
| P7 memory management | ~100 | 4-6 hours |
| P8 error handling | ~100 | 4-6 hours |
| P9 configuration | ~200 | 1 day |
| P10 tests | ~2000 | 5-7 days |
| **Total** | **~4600** | **~3-4 weeks** |

---

*Generated by brutal senior C++ engineer audit. No feelings were spared.*
