# C++ Improvement Ideas for Tightrope

Analysis of ~300 non-test source files across 12 modules.
Excludes: `vcpkg/`, `vcpkg_installed/`, `vendor/`.

---

## Priority 1 -- Highest ROI

### 1. Generic SQLite Row Mapper Template

**~200+ lines saved across 7 repositories**

Every repository repeats the same row-extraction loop 50+ times:

```cpp
while (stmt.executeStep()) {
    Record r;
    r.id    = stmt.getColumn(0).getInt64();
    r.name  = stmt.getColumn(1).getString();
    if (!stmt.getColumn(2).isNull()) r.email = stmt.getColumn(2).getString();
    results.push_back(std::move(r));
}
```

**Proposal:** Generic query executor + mapper function in `db/`:

```cpp
template <typename T>
std::vector<T> query_list(
    SQLite::Database& db,
    const char* sql,
    std::function<void(SQLite::Statement&)> bind_fn,
    std::function<T(SQLite::Statement&)> map_fn);

template <typename T>
std::optional<T> query_one(
    SQLite::Database& db,
    const char* sql,
    std::function<void(SQLite::Statement&)> bind_fn,
    std::function<T(SQLite::Statement&)> map_fn);

// Nullable column helpers to reduce per-field null checks:
inline std::optional<std::string> nullable_text(SQLite::Statement& s, int col);
inline std::optional<std::int64_t> nullable_int64(SQLite::Statement& s, int col);
```

**Applies to:** `account_repo`, `api_key_repo` (all 5 split files), `firewall_repo`, `request_log_repo`, `session_repo`, `settings_repo`.

---

### 2. Proxy Request Orchestrator Template

**~200 lines saved in `proxy_service.cpp`**

`collect_responses_json`, `collect_responses_compact`, and `stream_responses_sse` follow an identical 10-step flow differing only in plan builder, retry predicate, and result collector.

**Proposal:**

```cpp
struct OrchestratorPolicy {
    using PlanBuilder = std::function<UpstreamRequestPlan(/*route, body, headers*/)>;
    using RetryPred   = std::function<bool(const UpstreamResult&)>;
    using Collector   = std::function<ProxyResult(const UpstreamResult&)>;

    PlanBuilder  build_plan;
    RetryPred    should_retry;
    Collector    collect;
    int          retry_budget = 1;
};

ProxyResult orchestrate(
    const std::string& route,
    const std::string& body,
    const HeaderMap& headers,
    const OrchestratorPolicy& policy);
```

Each handler becomes a one-liner calling `orchestrate()` with its specific policy.

---

### 3. Admin Route Wiring Helpers

**~200 lines saved across 4 admin runtime files**

`admin_api_keys_runtime.cpp`, `admin_accounts_runtime.cpp`, `admin_settings_runtime.cpp`, and `admin_oauth_runtime.cpp` repeat the same route wiring lambda patterns for GET, POST, PATCH, DELETE.

**Proposal:**

```cpp
template <typename Handler>
void wire_post(uWS::App& app, const char* path, Handler&& handler);

template <typename Handler>
void wire_get(uWS::App& app, const char* path, Handler&& handler);

template <typename Handler>
void wire_patch_with_param(uWS::App& app, const char* path, Handler&& handler);

template <typename Handler>
void wire_del_with_param(uWS::App& app, const char* path, Handler&& handler);
```

Each route registration becomes one line instead of 5-8.

---

### 4. Use glaze for Controller JSON Responses

**~300 lines saved, eliminates manual string concat bugs**

Admin runtime files build JSON via manual concatenation:

```cpp
std::string(R"({"id":)") + std::to_string(x.id)
    + R"(,"name":)" + quote(x.name) + ...
```

This is error-prone (missed commas, unescaped strings). glaze is already a dependency -- define `glz::meta` for response structs and serialize automatically. Also eliminates the duplicated `optional_string_json()` / `nullable_json()` helper functions (see #11).

---

### 5. Fill Empty `Result<T>` / Error Type Stubs

**Better error propagation across the entire codebase**

`core/error/app_error.h`, `error_codes.h`, and `core/types/result.h` are all empty stubs. The codebase uses `std::optional<T>` everywhere, losing error context. Controllers already define ad-hoc status/code/message patterns.

**Proposal** (C++20 compatible, or use `std::expected` polyfill):

```cpp
enum class ErrorCode : int {
    DbUnavailable = 1000,
    NotFound       = 1001,
    InvalidInput   = 1002,
    AuthFailed     = 1003,
    Timeout        = 1004,
};

struct AppError {
    ErrorCode code;
    std::string message;
};

template <typename T>
using Result = std::variant<T, AppError>;
```

Migrate repositories incrementally -- callers can distinguish "not found" from "database error" without guessing.

---

### 6. Typed Exception Catches in Repositories

**Debuggability -- zero lines saved but eliminates silent failures**

All repos use `catch (...)` to swallow exceptions:

```cpp
try { /* ... */ } catch (...) { return std::nullopt; }
```

This loses diagnostic info. Catch `SQLite::Exception` specifically and log:

```cpp
try {
    /* ... */
} catch (const SQLite::Exception& e) {
    LOG_DEBUG("repo query failed: {}", e.what());
    return std::nullopt;
}
```

---

## Priority 2 -- Good ROI

### 7. CRTP Strategy Picker Base

**~135 lines saved across 9 strategies**

Every balancer strategy repeats the dual-signature boilerplate: `pick(CandidateView)` for real logic and `pick(vector<AccountCandidate>&, EligibilityOptions&)` that filters and delegates. This is identical in all 9 strategies: round_robin, weighted_round_robin, power_of_two, least_outstanding, latency_ewma, cost_aware, deadline_aware, headroom, success_rate.

**Proposal:**

```cpp
template <typename Derived>
struct StrategyBase {
    const AccountCandidate* pick(
        const std::vector<AccountCandidate>& accounts,
        const EligibilityOptions& opts) const {
        auto eligible = filter_eligible_accounts(accounts, opts);
        return static_cast<const Derived*>(this)->pick_eligible(eligible);
    }
};
```

---

### 8. Generic TTL Cache

**~80 lines saved, reusable across modules**

`SessionBridge` and `StickyResolver` both implement TTL-based caches with identical structure: `unordered_map<K, Entry{V, updated_at_ms}>` with `upsert`, `find` (returns nullptr if expired), and `purge_stale`.

**Proposal:**

```cpp
template <typename K, typename V>
class TTLCache {
public:
    explicit TTLCache(std::int64_t ttl_ms);
    void upsert(const K& key, V value, std::int64_t now_ms);
    const V* find(const K& key, std::int64_t now_ms) const;
    bool erase(const K& key);
    std::size_t purge_stale(std::int64_t now_ms);
    void clear();
    [[nodiscard]] std::size_t size() const;
};
```

Replaces `proxy/session/session_bridge` internal map and `proxy/session/sticky_resolver` internal map.

---

### 9. Declarative Schema Migration

**~150 lines saved across 8 repos**

All repos implement `ensure_*_schema()` with the same pattern: CREATE TABLE + ensure_column for each migration. A declarative approach:

```cpp
struct ColumnMigration {
    const char* table;
    const char* column;
    const char* alter_sql;
};

bool ensure_schema(
    sqlite3* db,
    const char* create_table_sql,
    std::initializer_list<ColumnMigration> migrations);
```

Especially impactful for `api_key_repo_schema.cpp` (12 ALTER statements) and `settings_repo.cpp` (30+ ALTERs).

---

### 10. Shared Case-Insensitive Header Lookup

**4 independent copies eliminated (~40 lines)**

Four middleware files independently implement `find_header_case_insensitive()`:
- `api_key_filter.cpp`
- `firewall_filter.cpp`
- `request_id.cpp`
- `decompression.cpp`

All iterate `HeaderMap` doing `equals_case_insensitive()`. Move to `core/text/ascii.h` (which already exists):

```cpp
inline std::optional<std::string_view> find_header(
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string_view key);
```

---

### 11. Consolidated `nullable_json` Helpers

**3 separate definitions eliminated**

`admin_runtime_common.cpp` defines `optional_string_json()`. `admin_accounts_runtime.cpp` redefines it identically. `admin_oauth_runtime.cpp` defines `nullable_json()` (same logic). Consolidate into one set of overloads in `admin_runtime_common.h`.

This becomes moot if #4 (glaze serialization) is adopted.

---

### 12. Unified JSON Serialization Wrapper

**3 wrappers consolidated into 1**

`proxy_service.cpp`, `chat_completions_service.cpp`, and `proxy_service_helpers.cpp` each define their own `serialize_json()` / `write_json_payload()` around `glz::write_json()`.

```cpp
template <typename T>
std::optional<std::string> safe_write(const T& value);
```

Place in `core/json/serialize.h` (currently an empty stub).

---

### 13. Remove `api_key_repo_internal.h` Delegation Layer

**~50 lines of dead wrappers**

`api_key_repo_internal.h` re-declares `exec_sql()`, `has_column()`, `ensure_column()`, `bind_optional_text()` that simply delegate to `sqlite_repo_utils`. The 5 `api_key_repo_*.cpp` files should use `sqlite_repo_utils` directly.

---

## Priority 3 -- Architectural Improvements

### 14. C++20 Concepts for Strategy Interface

The balancer uses a concept-based design implicitly (no virtual functions). Making it explicit enables better compiler diagnostics:

```cpp
template <typename S>
concept BalancerStrategy = requires(const S& s, CandidateView cv) {
    { s.pick_eligible(cv) } -> std::convertible_to<const AccountCandidate*>;
};
```

---

### 15. Shared Headroom Calculation

The "headroom = 1 - clamped_usage * weight" calculation appears in `scorer.cpp` (`usage_weight_from_account`), `weighted_round_robin.cpp` (same computation), and `headroom.cpp` (dual-weight variant). Extract into a shared utility:

```cpp
inline double headroom(double usage_ratio, double static_weight = 1.0);
inline double headroom_dual(double primary, double secondary, double w1, double w2);
```

---

### 16. Score-Based Picker Helper

Four strategies (least_outstanding, latency_ewma, headroom, cost_aware fallback) use `pick_min_ptr`/`pick_max_ptr` with one-line scorer lambdas. A thin wrapper makes intent explicit:

```cpp
template <auto Extractor, bool Maximize = false>
const AccountCandidate* pick_by_metric(CandidateView candidates);
```

---

### 17. Middleware Chain Template

`server_routes.cpp` chains middleware manually with early-return:

```cpp
if (!firewall_filter(...)) return;
if (!api_key_filter(...)) return;
```

A composable chain avoids deep nesting and makes ordering explicit:

```cpp
class MiddlewareChain {
public:
    MiddlewareChain& add(Filter f);
    std::optional<FilterResult> evaluate(const RequestContext& ctx) const;
};
```

---

### 18. Protected Path Registry

`api_key_filter.cpp` and `firewall_filter.cpp` each define nearly-identical `is_*_protected_path()` functions checking `/v1`, `/backend-api/codex`, `/backend-api/transcribe`. Consolidate into a single shared function in `server/middleware/`.

---

### 19. CRDT Base Concept

All 4 CRDTs (GCounter, PNCounter, LWWRegister, ORSet) implement `merge()` but share no interface. A concept enables generic algorithms:

```cpp
template <typename T>
concept MergeableCrdt = requires(T& a, const T& b) {
    { a.merge(b) } -> std::same_as<void>;
};
```

---

### 20. CRDT Persistence Layer Template

`crdt_store.cpp` manually manages `sqlite3_stmt*` for each CRDT type with the same prepare/bind/step/finalize pattern. A statement RAII wrapper + generic persistence template would cut ~100 lines:

```cpp
struct StmtDeleter {
    void operator()(sqlite3_stmt* s) const { sqlite3_finalize(s); }
};
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtDeleter>;
```

---

### 21. Composite Score Builder

`deadline_aware` and `power_of_two` both call `compute_composite_score()` from `scorer.cpp` but apply different weight adjustments. A builder/modifier pattern makes this explicit:

```cpp
ScoreWeights deadline_adjusted(const ScoreWeights& base, double urgency_factor);
```

This already partially exists (`adjusted_deadline_weights`) -- extend the pattern and document it as the canonical way to customize scoring.

---

### 22. Transaction RAII Guard for Raw `sqlite3`

`sync_engine.cpp` and `crdt_store.cpp` both manually call `BEGIN IMMEDIATE` / `COMMIT` / `ROLLBACK`. `crdt_store.cpp` has `in_transaction(db, fn)` but `sync_engine.cpp` doesn't use it. Unify:

```cpp
class TransactionGuard {
public:
    explicit TransactionGuard(sqlite3* db);
    void commit();
    ~TransactionGuard(); // ROLLBACK if not committed
};
```

---

### 23. Upstream Header Builder Unification

`upstream_headers.cpp` provides 3 header builder functions: `build_upstream_headers()`, `build_upstream_transcribe_headers()`, `build_upstream_websocket_headers()`. All share header filtering, token injection, and account ID addition. Consolidate with a mode enum:

```cpp
enum class UpstreamMode { Http, Transcribe, WebSocket };

HeaderMap build_upstream_headers(
    const HeaderMap& inbound,
    const UpstreamHeaderOptions& opts);
```

---

### 24. Error Event Builder Consolidation

`provider_contract.h` exposes 3 similar error event builders:
- `build_websocket_error_event_json()`
- `build_response_failed_event_json()`
- `build_websocket_response_failed_event_json()`

These share ~80% of logic. A single parameterized builder with an event type enum eliminates the duplication.

---

### 25. Strategy Factory / Registry

Currently strategy selection is likely a switch/if chain. A runtime registry enables dynamic selection from config:

```cpp
using PickerFn = std::function<const AccountCandidate*(
    const std::vector<AccountCandidate>&, const EligibilityOptions&)>;

const std::unordered_map<std::string_view, PickerFn>& strategy_registry();
```

---

### 26. Typed N-API Async Binder

`addon.cpp` already has `queue_async_value<Value, Converter>`. The `bridge_*.cpp` files repeat: parse Napi::Object -> extract fields -> call Bridge method -> convert result. A higher-level binder reduces this:

```cpp
template <typename Input, typename Output, typename Parser, typename Converter>
Napi::Value bind_method(
    const Napi::CallbackInfo& info,
    Parser&& parse_input,
    auto (Bridge::*method)(const Input&),
    Converter&& convert_output);
```

---

### 27. Stream Handler Concept

`proxy/stream/` has 4 handlers (sse, ws, compact, transcribe) that compose OpenAI provider contracts and build response payloads. A concept defining the handler interface makes them interchangeable:

```cpp
template <typename T>
concept StreamHandler = requires(T& h, const std::string& route) {
    { h.build_response(route) } -> std::convertible_to<std::vector<std::string>>;
};
```

---

## Safety & Performance

### 28. Endianness in RPC Channel Frame Encoding

`rpc_channel.cpp` uses `memcpy` for encoding `u16` channel and `u32` payload size into the wire buffer. This assumes matching endianness between peers. For cross-platform safety, use `htons`/`htonl` or C++23 `std::byteswap`.

---

### 29. Validate `table_name` in `SyncEngine::apply_batch`

`sync_engine.cpp` applies journal entries by table name from remote peers. If `table_name` is not validated against an allowlist, crafted table names could enable SQL injection. Verify this is hardened.

---

### 30. Reserve Vectors in Repository List Queries

List queries (`list_accounts`, `list_api_keys`, `list_firewall_entries`, etc.) push_back into vectors without reserving. When a `COUNT(*)` is cheap or an upper bound is known, `reserve()` avoids repeated reallocations.

---

### 31. Fill Empty Core Stubs

Several core files are empty stubs that represent incomplete infrastructure:

| File | Purpose | Impact |
|------|---------|--------|
| `core/types/result.h` | Result<T,E> type | See #5 |
| `core/types/account.h` | Account struct | Types duplicated in repos |
| `core/types/session.h` | Session struct | Types duplicated in repos |
| `core/types/api_key.h` | ApiKey struct | Types duplicated in repos |
| `core/types/request.h` | Request struct | Types duplicated in repos |
| `core/types/settings.h` | Settings struct | Types duplicated in repos |
| `core/time/clock.h` | Mockable clock | Needed for testable time |
| `core/time/ewma.h` | EWMA template | Duplicated in balancer |
| `core/json/serialize.h` | JSON templates | See #12 |
| `auth/crypto/fernet.h` | Fernet encryption | Not yet implemented |
| `auth/crypto/key_file.h` | Key file I/O | Not yet implemented |
| `usage/cost_calculator.h` | Cost computation | Not yet implemented |
| `usage/usage_updater.h` | Background updater | Not yet implemented |
| `usage/trend_aggregator.h` | Trend aggregation | Not yet implemented |
| `sync/transport/lz4_codec.h` | Batch compression | Not yet implemented |

Filling these prevents type definitions from being duplicated across repos and controllers.

---

### 32. Use `std::format` for JSON String Building

Where glaze adoption (#4) is too heavy, `std::format` (C++20) is safer and faster than manual `R"({"id":)" + std::to_string(...)` concatenation. Eliminates missed-comma and unescaped-string bugs.

---

## Summary

| # | Category | Idea | Estimated Savings |
|---|----------|------|-------------------|
| 1 | Template | SQLite row mapper | ~200 lines |
| 2 | Template | Proxy request orchestrator | ~200 lines |
| 3 | Template | Admin route wiring helpers | ~200 lines |
| 4 | Arch | glaze for controller JSON | ~300 lines + bug fixes |
| 5 | Modernize | Result<T> error type | better errors codebase-wide |
| 6 | Safety | Typed exception catches | debuggability |
| 7 | Template | CRTP strategy base | ~135 lines |
| 8 | Template | Generic TTL cache | ~80 lines |
| 9 | Template | Declarative schema migration | ~150 lines |
| 10 | Dedup | Shared header lookup | ~40 lines |
| 11 | Dedup | Consolidated nullable_json | ~30 lines |
| 12 | Dedup | Unified JSON write wrapper | ~30 lines |
| 13 | Dedup | Remove api_key_repo_internal | ~50 lines |
| 14 | Modernize | C++20 concepts for strategies | compiler safety |
| 15 | Dedup | Shared headroom calculation | ~30 lines |
| 16 | Template | Score-based picker helper | ~20 lines |
| 17 | Arch | Middleware chain template | extensibility |
| 18 | Dedup | Protected path registry | ~20 lines |
| 19 | Modernize | CRDT base concept | generic algorithms |
| 20 | Template | CRDT persistence template | ~100 lines |
| 21 | Arch | Composite score builder | extensibility |
| 22 | Template | Transaction RAII guard | safety |
| 23 | Dedup | Upstream header unification | ~60 lines |
| 24 | Dedup | Error event builder consolidation | ~40 lines |
| 25 | Arch | Strategy factory/registry | configurability |
| 26 | Template | Typed N-API binder | ~60 lines |
| 27 | Arch | Stream handler concept | extensibility |
| 28 | Safety | Endianness in RPC framing | correctness |
| 29 | Safety | Table name validation in sync | security |
| 30 | Perf | Reserve vectors in list queries | allocation reduction |
| 31 | Arch | Fill empty core stubs | eliminates type duplication |
| 32 | Modernize | std::format for JSON building | safety + readability |

**Estimated total: ~1,500+ lines of duplicated code removable, plus significant type-safety and maintainability gains.**
