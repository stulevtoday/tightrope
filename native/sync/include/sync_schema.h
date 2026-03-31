#pragma once
// Create sync infrastructure tables and HLC columns.

#include <sqlite3.h>

namespace tightrope::sync {

// Apply WAL mode and full synchronous to the connection.
// Safe to call multiple times; idempotent.
bool ensure_sync_durability(sqlite3* db);

// Creates sync tables and adds _hlc_* columns to replicated app tables.
// Also applies durability PRAGMAs (WAL + synchronous=FULL).
// Safe to call multiple times.
bool ensure_sync_schema(sqlite3* db);

} // namespace tightrope::sync
