#pragma once
// Register HLC and checksum SQLite functions used by CDC triggers.

#include <sqlite3.h>

namespace tightrope::sync {

class HlcClock;

// Registers _hlc_now_wall(), _hlc_now_counter(), _hlc_site_id(), and _checksum()
// on the given connection. `clock` must outlive any use of the registered functions.
bool register_hlc_functions(sqlite3* db, HlcClock* clock);

} // namespace tightrope::sync
