#include <catch2/catch_test_macros.hpp>

#include <string>

#include "cdc_triggers.h"

TEST_CASE("cdc trigger generator emits insert update delete trigger SQL", "[sync][cdc]") {
    const tightrope::sync::CdcTableSpec spec = {
        .table_name = "accounts",
        .primary_key_columns = {"id"},
        .tracked_columns = {"email", "provider"},
    };

    const auto sql = tightrope::sync::build_cdc_triggers(spec);
    REQUIRE(sql.insert_trigger.find("CREATE TRIGGER _cdc_accounts_insert AFTER INSERT ON accounts") != std::string::npos);
    REQUIRE(sql.insert_trigger.find(R"(json_object('id', NEW.id))") != std::string::npos);
    REQUIRE(sql.insert_trigger.find("INSERT INTO _sync_journal") != std::string::npos);
    REQUIRE(sql.insert_trigger.find("UPDATE accounts SET _hlc_wall = _hlc_now_wall()") != std::string::npos);

    REQUIRE(sql.update_trigger.find("CREATE TRIGGER _cdc_accounts_update AFTER UPDATE ON accounts") != std::string::npos);
    REQUIRE(sql.update_trigger.find("WHEN OLD._hlc_wall != _hlc_now_wall() OR OLD._hlc_counter != _hlc_now_counter()") !=
            std::string::npos);
    REQUIRE(sql.update_trigger.find(R"(json_object('email', OLD.email, 'provider', OLD.provider))") !=
            std::string::npos);
    REQUIRE(sql.update_trigger.find(R"(json_object('email', NEW.email, 'provider', NEW.provider))") !=
            std::string::npos);

    REQUIRE(sql.delete_trigger.find("CREATE TRIGGER _cdc_accounts_delete AFTER DELETE ON accounts") != std::string::npos);
    REQUIRE(sql.delete_trigger.find("INSERT OR REPLACE INTO _sync_tombstones") != std::string::npos);
    REQUIRE(sql.delete_trigger.find(R"(VALUES ('accounts', json_object('id', OLD.id), _hlc_now_wall(), _hlc_site_id()))") !=
            std::string::npos);
}
