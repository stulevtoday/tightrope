#pragma once

#include <sqlite3.h>

namespace SQLite {
class Database;
}

// SQLite integrity checks

namespace tightrope::db {

[[nodiscard]] bool run_integrity_check(SQLite::Database& db) noexcept;
[[nodiscard]] bool run_integrity_check(sqlite3* db) noexcept;

} // namespace tightrope::db
