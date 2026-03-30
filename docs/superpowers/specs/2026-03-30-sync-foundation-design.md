# Sync Foundation -- Sub-project 1 Design

Fixes the prerequisite infrastructure gaps in the consensus/journal system so that
CDC triggers, persistent journaling, and cross-node replication can work end-to-end.

## Context

The sync subsystem has a correct Raft consensus layer, HLC implementation, conflict
resolution model, wire protocol, and CDC trigger generator. However several pieces of
glue are missing:

- CDC triggers call four custom SQLite functions that are never registered.
- Tracked tables lack the `_hlc_wall`, `_hlc_counter`, `_hlc_site` columns the triggers write.
- The `_sync_tombstones` table referenced by delete triggers does not exist.
- The in-memory `Journal` class is disconnected from the `_sync_journal` SQLite table.
- `rpc_channel.cpp` uses native-endian encoding while `sync_protocol.cpp` uses little-endian.

This sub-project fixes all five. Sub-project 2 (replay engine) and Sub-project 3
(transport/consensus) build on top of it.

---

## 1. Custom SQLite Function Registration

### Files

- `native/sync/include/hlc_functions.h` (new)
- `native/sync/src/hlc_functions.cpp` (new)

### Interface

```cpp
namespace tightrope::sync {

// Registers _hlc_now_wall(), _hlc_now_counter(), _hlc_site_id(), _checksum()
// on the given connection. clock must outlive all subsequent calls.
bool register_hlc_functions(sqlite3* db, HlcClock* clock);

} // namespace tightrope::sync
```

### Behavior

| Function | Args | Returns | Implementation |
|----------|------|---------|----------------|
| `_hlc_now_wall()` | 0 | INTEGER | `clock->on_local_event(now_ms()).wall` |
| `_hlc_now_counter()` | 0 | INTEGER | `clock->snapshot().counter` (after wall call) |
| `_hlc_site_id()` | 0 | INTEGER | `clock->snapshot().site_id` |
| `_checksum(table, pk, op, old, new)` | 5 TEXT | TEXT | `journal_checksum(...)` |

`_hlc_now_wall` and `_hlc_now_counter` work as a pair: within a single trigger
execution the wall call advances the clock once, and counter reads back the result.
The clock pointer is stored via `sqlite3_user_data`.

Wall time source: `std::chrono::system_clock::now()` converted to milliseconds.

### Tests

`tests/unit/sync/hlc_functions_test.cpp`

- Register on in-memory db, `SELECT _hlc_site_id()` returns configured site_id.
- `SELECT _hlc_now_wall()` returns a value close to current time in ms.
- `SELECT _hlc_now_counter()` increments on successive calls.
- `SELECT _checksum('t','pk','INSERT',NULL,'{}')` matches `journal_checksum()`.
- Calling without registration returns SQL error (negative test).

---

## 2. Sync Schema

### Files

- `native/sync/include/sync_schema.h` (new)
- `native/sync/src/sync_schema.cpp` (new)

### Interface

```cpp
namespace tightrope::sync {

bool ensure_sync_schema(sqlite3* db);

} // namespace tightrope::sync
```

### Behavior

1. Creates `_sync_journal` table if not exists (existing schema from tests).
2. Creates `_sync_tombstones`:
   ```sql
   CREATE TABLE IF NOT EXISTS _sync_tombstones (
     table_name TEXT NOT NULL,
     row_pk     TEXT NOT NULL,
     deleted_at INTEGER NOT NULL,
     site_id    INTEGER NOT NULL,
     PRIMARY KEY (table_name, row_pk)
   );
   ```
3. For each table returned by `replicated_table_names()`, adds `_hlc_wall INTEGER DEFAULT 0`,
   `_hlc_counter INTEGER DEFAULT 0`, `_hlc_site INTEGER DEFAULT 0` if not already present.
   Uses PRAGMA `table_info` to check existence before ALTER.

Silently succeeds if columns/tables already exist (idempotent).

### Dependency

Requires `replicated_table_names()` from conflict_resolver (see section 5).

### Tests

`tests/integration/sync/sync_schema_test.cpp`

- Create minimal app tables (accounts, dashboard_settings, api_keys, api_key_limits,
  ip_allowlist, usage_history, sticky_sessions) in a temp db.
- Call `ensure_sync_schema()`.
- Verify `_sync_journal` and `_sync_tombstones` exist.
- Verify each replicated table has `_hlc_wall`, `_hlc_counter`, `_hlc_site` columns.
- Verify non-replicated tables (request_logs) do NOT get HLC columns.
- Call `ensure_sync_schema()` a second time -- idempotent, no errors.

---

## 3. Persistent Journal

### Files

- `native/sync/include/persistent_journal.h` (new)
- `native/sync/src/persistent_journal.cpp` (new)

### Interface

```cpp
namespace tightrope::sync {

class PersistentJournal {
public:
    explicit PersistentJournal(sqlite3* db);

    std::optional<JournalEntry> append(const PendingJournalEntry& entry);
    std::vector<JournalEntry> entries_after(std::uint64_t after_seq,
                                            std::size_t limit = 1000) const;
    std::vector<JournalEntry> rollback_batch(std::string_view batch_id);
    bool mark_applied(std::uint64_t seq, int applied_value);
    std::size_t compact(std::uint64_t cutoff_wall, std::uint64_t max_ack_seq);
    std::size_t size() const;

private:
    sqlite3* db_;
};

} // namespace tightrope::sync
```

### Behavior

- **append**: INSERT into `_sync_journal` with auto-assigned seq (INTEGER PRIMARY KEY
  without explicit value lets SQLite assign). Computes checksum. Generates batch_id if
  empty. Returns the full entry with assigned seq, or nullopt on error.
- **entries_after**: SELECT WHERE seq > ? ORDER BY seq ASC LIMIT ?.
- **rollback_batch**: DELETE WHERE batch_id = ? RETURNING all columns. Returns in
  reverse seq order. If RETURNING is unavailable (SQLite < 3.35), SELECT then DELETE.
- **mark_applied**: UPDATE SET applied = ? WHERE seq = ?. Returns true if row changed.
- **compact**: DELETE WHERE hlc_wall < ? AND seq <= ?. Returns count of deleted rows.
- **size**: SELECT COUNT(*) FROM _sync_journal.

All operations use prepared statements. Errors are caught and return empty/false/nullopt.

### Relationship to existing Journal class

The in-memory `Journal` class stays untouched -- it remains useful for pure unit tests.
`PersistentJournal` is the production counterpart that reads/writes `_sync_journal`.
`SyncEngine::build_batch()` and `apply_batch()` already operate on `_sync_journal`
directly and remain unchanged.

### Tests

`tests/integration/sync/persistent_journal_test.cpp`

- Append entries, close db, reopen, verify entries survive.
- `entries_after(0)` returns all entries in seq order.
- `entries_after(N)` returns only entries after N.
- Append multiple entries, verify seq is monotonically increasing.
- `rollback_batch` removes correct entries, returns in reverse order, leaves others.
- `mark_applied` changes flag, verifiable via entries_after.
- `compact` removes old acknowledged entries, keeps recent ones.
- `size()` reflects current count after appends, rollbacks, compaction.
- Append with empty batch_id auto-generates a valid UUID batch_id.

---

## 4. RPC Channel Endianness Fix

### Files

- `native/sync/src/transport/rpc_channel.cpp` (edit)

### Change

Replace `memcpy`-based encoding/decoding with explicit little-endian byte operations:

```cpp
// write_u16: push low byte, then high byte
// write_u32: push bytes 0-3 from LSB to MSB
// read_u16: reconstruct from bytes[0] | (bytes[1] << 8)
// read_u32: reconstruct from bytes[0..3] with shifts
```

Matches the encoding style already used in `sync_protocol.cpp`.

### Tests

Addition to existing `tests/unit/sync/rpc_channel_test.cpp`:

- Encode a frame with channel=0x0102 and verify wire bytes are [0x02, 0x01, ...] (LE).
- Encode a frame with payload_size that exercises all 4 bytes of the u32.
- Existing round-trip tests continue to pass (regression).

---

## 5. Expose Replicated Table Names

### Files

- `native/sync/include/conflict_resolver.h` (edit)
- `native/sync/src/conflict_resolver.cpp` (edit)

### Change

Add to header:

```cpp
std::vector<std::string_view> replicated_table_names();
```

Implementation: iterate `kTableRules`, collect entries where `replicated == true`.

### Tests

Addition to existing `tests/unit/sync/conflict_resolver_test.cpp`:

- `replicated_table_names()` contains accounts, dashboard_settings, api_keys,
  api_key_limits, ip_allowlist, usage_history, sticky_sessions.
- Does NOT contain request_logs, _sync_journal, _sync_meta, _sync_last_seen.

---

## Implementation Order

```
#5 (expose table names)
    |
    v
#1 (SQLite functions)  #4 (endianness fix)
    |                        |
    v                        v
#2 (sync schema)       rpc_channel_test additions
    |
    v
#3 (persistent journal)
```

#4 and #5 are independent of each other. #1 and #2 can proceed in parallel after #5.
#3 depends on #2 (needs `_sync_journal` table created by `ensure_sync_schema`).

## Out of Scope

- Replay engine (applying journal entries to app tables) -- Sub-project 2.
- Leader forwarding RPC, snapshot transfer, transport reconnection -- Sub-project 3.
- Installing CDC triggers on live databases -- Sub-project 2 (needs replay first).
