#include "sqlite_registry.h"

#include <mutex>
#include <unordered_map>

#include <SQLiteCpp/Database.h>

namespace tightrope::db::connection {

namespace {

std::mutex& registry_mutex() {
    static auto* value = new std::mutex();
    return *value;
}

std::unordered_map<sqlite3*, SQLite::Database*>& registry() {
    static auto* value = new std::unordered_map<sqlite3*, SQLite::Database*>();
    return *value;
}

} // namespace

void register_database(SQLite::Database& db) noexcept {
    auto* handle = db.getHandle();
    if (handle == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    registry()[handle] = &db;
}

void unregister_database(sqlite3* handle) noexcept {
    if (handle == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    registry().erase(handle);
}

SQLite::Database* lookup_database(sqlite3* handle) noexcept {
    if (handle == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(registry_mutex());
    const auto it = registry().find(handle);
    return it == registry().end() ? nullptr : it->second;
}

} // namespace tightrope::db::connection
