#pragma once
// Create sync infrastructure tables and HLC columns.

#include <sqlite3.h>

namespace tightrope::sync {

// Creates sync tables and adds _hlc_* columns to replicated app tables.
// Safe to call multiple times.
bool ensure_sync_schema(sqlite3* db);

} // namespace tightrope::sync
