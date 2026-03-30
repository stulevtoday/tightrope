#pragma once

#include <sqlite3.h>

namespace tightrope::sync::consensus::nuraft_backend::internal {

class SqliteStatement final {
public:
    SqliteStatement(sqlite3* db, const char* sql) {
        rc_ = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
    }
    ~SqliteStatement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
    }
    [[nodiscard]] bool ok() const noexcept {
        return rc_ == SQLITE_OK && stmt_ != nullptr;
    }
    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return stmt_;
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
    int rc_ = SQLITE_ERROR;
};

} // namespace tightrope::sync::consensus::nuraft_backend::internal
