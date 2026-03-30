#pragma once

#include <memory>

#include <sqlite3.h>

#include "connection/sqlite_pool.h"

namespace tightrope::server::controllers {

struct ControllerDbHandle {
    std::unique_ptr<db::SqlitePool> owned_pool;
    sqlite3* db = nullptr;
};

ControllerDbHandle open_controller_db(sqlite3* external_db = nullptr);

} // namespace tightrope::server::controllers
