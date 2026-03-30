#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "connection/sqlite_pool.h"
#include "migration/integrity_check.h"
#include "migration/migration_runner.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-db-XXXXXX";
    int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

} // namespace

TEST_CASE("baseline migration creates schema_version", "[db][migrations]") {
    auto db_path = make_temp_db_path();

    tightrope::db::SqlitePool pool(db_path);
    REQUIRE(pool.open());

    auto* db = pool.connection();
    REQUIRE(db != nullptr);

    REQUIRE(tightrope::db::run_migrations(db));
    REQUIRE(tightrope::db::table_exists(db, "schema_version"));
    REQUIRE(tightrope::db::run_integrity_check(db));

    pool.close();
    std::remove(db_path.c_str());
}
