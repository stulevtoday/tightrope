#pragma once

#include <memory>
#include <string>

#include "db_pool.h"

// SQLite connection pool

namespace SQLite {
class Database;
}

namespace tightrope::db {

class SqlitePool final : public DbPool {
  public:
    explicit SqlitePool(std::string db_path);
    ~SqlitePool() override;

    bool open() noexcept;
    void close() noexcept;

    [[nodiscard]] sqlite3* connection() const noexcept override;
    [[nodiscard]] SQLite::Database* database() const noexcept;
    [[nodiscard]] const std::string& db_path() const noexcept;

  private:
    std::string db_path_;
    std::unique_ptr<SQLite::Database> db_;
};

} // namespace tightrope::db
