#include "sqlite_pool.h"

#include <SQLiteCpp/Database.h>

#include <utility>

#include "sqlite_registry.h"

namespace tightrope::db {

SqlitePool::SqlitePool(std::string db_path) : db_path_(std::move(db_path)) {}

SqlitePool::~SqlitePool() {
    close();
}

bool SqlitePool::open() noexcept {
    if (db_ != nullptr && db_->getHandle() != nullptr) {
        return true;
    }

    try {
        db_ = std::make_unique<SQLite::Database>(
            db_path_,
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX
        );
        connection::register_database(*db_);
    } catch (...) {
        db_.reset();
        return false;
    }
    return true;
}

void SqlitePool::close() noexcept {
    if (db_ != nullptr) {
        connection::unregister_database(db_->getHandle());
        db_.reset();
    }
}

sqlite3* SqlitePool::connection() const noexcept {
    return db_ == nullptr ? nullptr : db_->getHandle();
}

SQLite::Database* SqlitePool::database() const noexcept {
    return db_.get();
}

const std::string& SqlitePool::db_path() const noexcept {
    return db_path_;
}

} // namespace tightrope::db
