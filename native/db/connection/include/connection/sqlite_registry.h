#pragma once

#include <sqlite3.h>

namespace SQLite {
class Database;
}

namespace tightrope::db::connection {

void register_database(SQLite::Database& db) noexcept;
void unregister_database(sqlite3* handle) noexcept;
[[nodiscard]] SQLite::Database* lookup_database(sqlite3* handle) noexcept;

} // namespace tightrope::db::connection
