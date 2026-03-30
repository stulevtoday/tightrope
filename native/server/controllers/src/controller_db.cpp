#include "controller_db.h"

#include "config_loader.h"

namespace tightrope::server::controllers {

ControllerDbHandle open_controller_db(sqlite3* external_db) {
    if (external_db != nullptr) {
        return {
            .owned_pool = nullptr,
            .db = external_db,
        };
    }

    auto config = config::load_config();
    std::string db_path = config.db_path.empty() ? std::string("store.db") : config.db_path;
    auto pool = std::make_unique<db::SqlitePool>(std::move(db_path));
    if (!pool->open()) {
        return {
            .owned_pool = nullptr,
            .db = nullptr,
        };
    }

    sqlite3* db = pool->connection();
    return {
        .owned_pool = std::move(pool),
        .db = db,
    };
}

} // namespace tightrope::server::controllers
