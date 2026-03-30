#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "persistent_journal.h"
#include "sync_schema.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-pjournal-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

sqlite3* open_db(const std::string& path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));
    return db;
}

tightrope::sync::PendingJournalEntry make_pending(
    const std::uint64_t wall,
    const std::uint32_t counter,
    const std::uint32_t site_id,
    std::string table = "accounts",
    std::string pk = R"({"id":"1"})",
    std::string op = "INSERT",
    std::string old_vals = "",
    std::string new_vals = R"({"email":"a@x.com"})",
    std::string batch_id = ""
) {
    tightrope::sync::PendingJournalEntry entry;
    entry.hlc = {.wall = wall, .counter = counter, .site_id = site_id};
    entry.table_name = std::move(table);
    entry.row_pk = std::move(pk);
    entry.op = std::move(op);
    entry.old_values = std::move(old_vals);
    entry.new_values = std::move(new_vals);
    entry.applied = 1;
    entry.batch_id = std::move(batch_id);
    return entry;
}

} // namespace

TEST_CASE("persistent journal append assigns monotonic seq and computes checksum", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);

    const auto e1 = journal.append(make_pending(100, 1, 7));
    REQUIRE(e1.has_value());
    REQUIRE(e1->seq > 0);
    REQUIRE_FALSE(e1->checksum.empty());
    REQUIRE(e1->checksum.size() == 64);

    const auto e2 = journal.append(make_pending(110, 2, 7));
    REQUIRE(e2.has_value());
    REQUIRE(e2->seq > e1->seq);

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal entries survive close and reopen", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();

    {
        auto* db = open_db(path);
        tightrope::sync::PersistentJournal journal(db);
        REQUIRE(journal.append(make_pending(100, 1, 7)).has_value());
        REQUIRE(journal.append(make_pending(110, 2, 7)).has_value());
        sqlite3_close(db);
    }

    {
        auto* db = open_db(path);
        tightrope::sync::PersistentJournal journal(db);
        REQUIRE(journal.size() == 2);
        const auto entries = journal.entries_after(0);
        REQUIRE(entries.size() == 2);
        REQUIRE(entries[0].hlc.wall == 100);
        REQUIRE(entries[1].hlc.wall == 110);
        sqlite3_close(db);
    }

    std::remove(path.c_str());
}

TEST_CASE("persistent journal entries_after returns subset by seq", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto e1 = journal.append(make_pending(100, 1, 7));
    const auto e2 = journal.append(make_pending(110, 2, 7));
    const auto e3 = journal.append(make_pending(120, 3, 7));
    REQUIRE(e1.has_value());
    REQUIRE(e2.has_value());
    REQUIRE(e3.has_value());

    const auto after_first = journal.entries_after(e1->seq);
    REQUIRE(after_first.size() == 2);
    REQUIRE(after_first[0].seq == e2->seq);
    REQUIRE(after_first[1].seq == e3->seq);

    const auto after_all = journal.entries_after(e3->seq);
    REQUIRE(after_all.empty());

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal rollback_batch removes correct entries", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    REQUIRE(journal.append(make_pending(100, 1, 7, "accounts", R"({"id":"1"})", "INSERT", "", R"({"e":"a"})", "batch-a"))
                .has_value());
    REQUIRE(journal.append(make_pending(110, 2, 7, "accounts", R"({"id":"2"})", "INSERT", "", R"({"e":"b"})", "batch-b"))
                .has_value());
    REQUIRE(journal.append(make_pending(120, 3, 7, "accounts", R"({"id":"3"})", "INSERT", "", R"({"e":"c"})", "batch-a"))
                .has_value());

    const auto removed = journal.rollback_batch("batch-a");
    REQUIRE(removed.size() == 2);
    REQUIRE(removed[0].seq > removed[1].seq);

    REQUIRE(journal.size() == 1);
    const auto remaining = journal.entries_after(0);
    REQUIRE(remaining.size() == 1);
    REQUIRE(remaining[0].hlc.wall == 110);

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal mark_applied updates flag", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto entry = journal.append(make_pending(100, 1, 7));
    REQUIRE(entry.has_value());
    REQUIRE(entry->applied == 1);

    REQUIRE(journal.mark_applied(entry->seq, 2));

    const auto entries = journal.entries_after(0);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].applied == 2);

    REQUIRE_FALSE(journal.mark_applied(999999, 2));

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal compact removes old acknowledged entries", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto e1 = journal.append(make_pending(100, 1, 7));
    const auto e2 = journal.append(make_pending(200, 2, 7));
    const auto e3 = journal.append(make_pending(300, 3, 7));
    REQUIRE(e1.has_value());
    REQUIRE(e2.has_value());
    REQUIRE(e3.has_value());

    const auto removed = journal.compact(250, e2->seq);
    REQUIRE(removed == 2);
    REQUIRE(journal.size() == 1);

    const auto remaining = journal.entries_after(0);
    REQUIRE(remaining.size() == 1);
    REQUIRE(remaining[0].seq == e3->seq);

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal append auto-generates batch_id when empty", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto entry = journal.append(make_pending(100, 1, 7));
    REQUIRE(entry.has_value());
    REQUIRE_FALSE(entry->batch_id.empty());
    REQUIRE(entry->batch_id.size() == 36);

    sqlite3_close(db);
    std::remove(path.c_str());
}
