#include "integrity_check.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <string_view>

#include "connection/sqlite_registry.h"

namespace tightrope::db {

bool run_integrity_check(SQLite::Database& db) noexcept {
    try {
        SQLite::Statement stmt(db, "PRAGMA quick_check;");
        if (!stmt.executeStep()) {
            return false;
        }
        const auto result = stmt.getColumn(0).getString();
        return result == "ok";
    } catch (...) {
        return false;
    }
}

bool run_integrity_check(sqlite3* db) noexcept {
    if (db == nullptr) {
        return false;
    }

    if (auto* sqlite_db = connection::lookup_database(db); sqlite_db != nullptr) {
        return run_integrity_check(*sqlite_db);
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA quick_check;", -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    int step = sqlite3_step(stmt);
    if (step != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    const unsigned char* text = sqlite3_column_text(stmt, 0);
    std::string_view result = text == nullptr ? std::string_view{} : std::string_view(reinterpret_cast<const char*>(text));
    sqlite3_finalize(stmt);

    return result == "ok";
}

} // namespace tightrope::db
