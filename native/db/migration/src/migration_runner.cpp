#include "migration_runner.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <exception>
#include <fstream>
#include <sstream>

#include "connection/sqlite_registry.h"

namespace tightrope::db {

namespace {

bool exec_sql(sqlite3* db, const char* sql) noexcept {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool exec_sql(SQLite::Database& db, const char* sql) noexcept {
    try {
        (void)db.exec(sql);
        return true;
    } catch (...) {
        return false;
    }
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.good()) {
        return {};
    }

    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

} // namespace

bool run_migrations(SQLite::Database& db, const std::string& migration_path) noexcept {
    std::string sql = read_file(migration_path);
    if (sql.empty()) {
        return false;
    }

    try {
        SQLite::Transaction txn(db);
        (void)db.exec(sql.c_str());
        txn.commit();
        return true;
    } catch (...) {
        return false;
    }
}

bool run_migrations(sqlite3* db, const std::string& migration_path) noexcept {
    if (db == nullptr) {
        return false;
    }

    if (auto* sqlite_db = connection::lookup_database(db); sqlite_db != nullptr) {
        return run_migrations(*sqlite_db, migration_path);
    }

    std::string sql = read_file(migration_path);
    if (sql.empty()) {
        return false;
    }
    if (!exec_sql(db, "BEGIN IMMEDIATE;")) {
        return false;
    }

    if (!exec_sql(db, sql.c_str())) {
        exec_sql(db, "ROLLBACK;");
        return false;
    }

    if (!exec_sql(db, "COMMIT;")) {
        exec_sql(db, "ROLLBACK;");
        return false;
    }

    return true;
}

bool table_exists(SQLite::Database& db, const std::string& table_name) noexcept {
    try {
        SQLite::Statement stmt(db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 LIMIT 1;");
        stmt.bind(1, table_name);
        return stmt.executeStep();
    } catch (...) {
        return false;
    }
}

bool table_exists(sqlite3* db, const std::string& table_name) noexcept {
    if (db == nullptr) {
        return false;
    }

    if (auto* sqlite_db = connection::lookup_database(db); sqlite_db != nullptr) {
        return table_exists(*sqlite_db, table_name);
    }

    constexpr const char* kSql = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return step == SQLITE_ROW;
}

} // namespace tightrope::db
