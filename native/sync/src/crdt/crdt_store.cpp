#include "crdt/crdt_store.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <boost/container/flat_map.hpp>

namespace tightrope::sync::crdt {

namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return {nullptr, sqlite3_finalize};
    }
    return {stmt, sqlite3_finalize};
}

bool exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool bind_i64(sqlite3_stmt* stmt, const int index, const std::int64_t value) {
    return sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
}

bool bind_text(sqlite3_stmt* stmt, const int index, const std::string& value) {
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool step_done(sqlite3_stmt* stmt) {
    return sqlite3_step(stmt) == SQLITE_DONE;
}

template <typename F>
bool in_transaction(sqlite3* db, F&& fn) {
    if (!exec_sql(db, "BEGIN IMMEDIATE;")) {
        return false;
    }
    if (!fn()) {
        exec_sql(db, "ROLLBACK;");
        return false;
    }
    if (!exec_sql(db, "COMMIT;")) {
        exec_sql(db, "ROLLBACK;");
        return false;
    }
    return true;
}

} // namespace

bool CrdtStore::ensure_schema(sqlite3* db) {
    if (db == nullptr) {
        return false;
    }

    constexpr const char* kStatements[] = {
        R"sql(
            CREATE TABLE IF NOT EXISTS _sync_crdt_pn_counter (
                key      TEXT    NOT NULL,
                site_id  INTEGER NOT NULL,
                positive INTEGER NOT NULL,
                negative INTEGER NOT NULL,
                PRIMARY KEY (key, site_id)
            );
        )sql",
        R"sql(
            CREATE TABLE IF NOT EXISTS _sync_crdt_lww (
                key         TEXT PRIMARY KEY,
                value       TEXT    NOT NULL,
                hlc_wall    INTEGER NOT NULL,
                hlc_counter INTEGER NOT NULL,
                hlc_site_id INTEGER NOT NULL,
                site_id     INTEGER NOT NULL
            );
        )sql",
        R"sql(
            CREATE TABLE IF NOT EXISTS _sync_crdt_or_set_tags (
                key         TEXT    NOT NULL,
                element     TEXT    NOT NULL,
                site_id     INTEGER NOT NULL,
                tag_counter INTEGER NOT NULL,
                PRIMARY KEY (key, element, site_id, tag_counter)
            );
        )sql",
        R"sql(
            CREATE TABLE IF NOT EXISTS _sync_crdt_or_set_counters (
                key      TEXT    NOT NULL,
                site_id  INTEGER NOT NULL,
                counter  INTEGER NOT NULL,
                PRIMARY KEY (key, site_id)
            );
        )sql",
    };

    for (const auto* sql : kStatements) {
        if (!exec_sql(db, sql)) {
            return false;
        }
    }
    return true;
}

bool CrdtStore::save_pn_counter(sqlite3* db, const std::string& key, const PNCounter& counter) {
    if (db == nullptr || !ensure_schema(db)) {
        return false;
    }

    return in_transaction(db, [&]() {
        auto delete_stmt = prepare(db, "DELETE FROM _sync_crdt_pn_counter WHERE key = ?1;");
        if (!delete_stmt || !bind_text(delete_stmt.get(), 1, key) || !step_done(delete_stmt.get())) {
            return false;
        }

        boost::container::flat_map<std::uint32_t, std::pair<std::int64_t, std::int64_t>> rows;
        for (const auto& [site, value] : counter.positive().counts()) {
            rows[site].first = value;
        }
        for (const auto& [site, value] : counter.negative().counts()) {
            rows[site].second = value;
        }

        auto insert_stmt = prepare(db, R"sql(
            INSERT INTO _sync_crdt_pn_counter (key, site_id, positive, negative)
            VALUES (?1, ?2, ?3, ?4);
        )sql");
        if (!insert_stmt) {
            return false;
        }

        for (const auto& [site, values] : rows) {
            sqlite3_reset(insert_stmt.get());
            sqlite3_clear_bindings(insert_stmt.get());
            if (!bind_text(insert_stmt.get(), 1, key) ||
                !bind_i64(insert_stmt.get(), 2, static_cast<std::int64_t>(site)) ||
                !bind_i64(insert_stmt.get(), 3, values.first) ||
                !bind_i64(insert_stmt.get(), 4, values.second) ||
                !step_done(insert_stmt.get())) {
                return false;
            }
        }
        return true;
    });
}

bool CrdtStore::load_pn_counter(sqlite3* db, const std::string& key, PNCounter& out) {
    if (db == nullptr || !ensure_schema(db)) {
        return false;
    }
    out = PNCounter{};

    auto stmt = prepare(db, R"sql(
        SELECT site_id, positive, negative
        FROM _sync_crdt_pn_counter
        WHERE key = ?1;
    )sql");
    if (!stmt || !bind_text(stmt.get(), 1, key)) {
        return false;
    }

    while (true) {
        const auto rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            return false;
        }

        out.set_site_counts(
            static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 0)),
            static_cast<std::int64_t>(sqlite3_column_int64(stmt.get(), 1)),
            static_cast<std::int64_t>(sqlite3_column_int64(stmt.get(), 2))
        );
    }
    return true;
}

bool CrdtStore::save_lww_string(sqlite3* db, const std::string& key, const LWWRegister<std::string>& reg) {
    if (db == nullptr || !ensure_schema(db)) {
        return false;
    }

    return in_transaction(db, [&]() {
        if (!reg.initialized()) {
            auto delete_stmt = prepare(db, "DELETE FROM _sync_crdt_lww WHERE key = ?1;");
            return delete_stmt && bind_text(delete_stmt.get(), 1, key) && step_done(delete_stmt.get());
        }

        auto stmt = prepare(db, R"sql(
            INSERT INTO _sync_crdt_lww (key, value, hlc_wall, hlc_counter, hlc_site_id, site_id)
            VALUES (?1, ?2, ?3, ?4, ?5, ?6)
            ON CONFLICT(key) DO UPDATE SET
                value       = excluded.value,
                hlc_wall    = excluded.hlc_wall,
                hlc_counter = excluded.hlc_counter,
                hlc_site_id = excluded.hlc_site_id,
                site_id     = excluded.site_id;
        )sql");
        if (!stmt) {
            return false;
        }

        return bind_text(stmt.get(), 1, key) &&
               bind_text(stmt.get(), 2, reg.value()) &&
               bind_i64(stmt.get(), 3, static_cast<std::int64_t>(reg.timestamp().wall)) &&
               bind_i64(stmt.get(), 4, static_cast<std::int64_t>(reg.timestamp().counter)) &&
               bind_i64(stmt.get(), 5, static_cast<std::int64_t>(reg.timestamp().site_id)) &&
               bind_i64(stmt.get(), 6, static_cast<std::int64_t>(reg.site_id())) && step_done(stmt.get());
    });
}

bool CrdtStore::load_lww_string(sqlite3* db, const std::string& key, LWWRegister<std::string>& out) {
    if (db == nullptr || !ensure_schema(db)) {
        return false;
    }
    out = LWWRegister<std::string>{};

    auto stmt = prepare(db, R"sql(
        SELECT value, hlc_wall, hlc_counter, hlc_site_id, site_id
        FROM _sync_crdt_lww
        WHERE key = ?1;
    )sql");
    if (!stmt || !bind_text(stmt.get(), 1, key)) {
        return false;
    }

    const auto rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
        return true;
    }
    if (rc != SQLITE_ROW) {
        return false;
    }

    const auto* value_text = sqlite3_column_text(stmt.get(), 0);
    out.set(
        value_text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(value_text)),
        Hlc{
            .wall = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 1)),
            .counter = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 2)),
            .site_id = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 3)),
        },
        static_cast<std::uint32_t>(sqlite3_column_int64(stmt.get(), 4))
    );
    return true;
}

bool CrdtStore::save_or_set(sqlite3* db, const std::string& key, const ORSet& set) {
    if (db == nullptr || !ensure_schema(db)) {
        return false;
    }

    return in_transaction(db, [&]() {
        auto delete_tags = prepare(db, "DELETE FROM _sync_crdt_or_set_tags WHERE key = ?1;");
        if (!delete_tags || !bind_text(delete_tags.get(), 1, key) || !step_done(delete_tags.get())) {
            return false;
        }

        auto delete_counters = prepare(db, "DELETE FROM _sync_crdt_or_set_counters WHERE key = ?1;");
        if (!delete_counters || !bind_text(delete_counters.get(), 1, key) || !step_done(delete_counters.get())) {
            return false;
        }

        auto insert_counter = prepare(db, R"sql(
            INSERT INTO _sync_crdt_or_set_counters (key, site_id, counter)
            VALUES (?1, ?2, ?3);
        )sql");
        if (!insert_counter) {
            return false;
        }
        for (const auto& [site_id, counter] : set.counters()) {
            sqlite3_reset(insert_counter.get());
            sqlite3_clear_bindings(insert_counter.get());
            if (!bind_text(insert_counter.get(), 1, key) ||
                !bind_i64(insert_counter.get(), 2, static_cast<std::int64_t>(site_id)) ||
                !bind_i64(insert_counter.get(), 3, static_cast<std::int64_t>(counter)) ||
                !step_done(insert_counter.get())) {
                return false;
            }
        }

        auto insert_tag = prepare(db, R"sql(
            INSERT INTO _sync_crdt_or_set_tags (key, element, site_id, tag_counter)
            VALUES (?1, ?2, ?3, ?4);
        )sql");
        if (!insert_tag) {
            return false;
        }
        for (const auto& [element, tags] : set.elements()) {
            for (const auto& tag : tags) {
                sqlite3_reset(insert_tag.get());
                sqlite3_clear_bindings(insert_tag.get());
                if (!bind_text(insert_tag.get(), 1, key) ||
                    !bind_text(insert_tag.get(), 2, element) ||
                    !bind_i64(insert_tag.get(), 3, static_cast<std::int64_t>(tag.site_id)) ||
                    !bind_i64(insert_tag.get(), 4, static_cast<std::int64_t>(tag.counter)) ||
                    !step_done(insert_tag.get())) {
                    return false;
                }
            }
        }

        return true;
    });
}

bool CrdtStore::load_or_set(sqlite3* db, const std::string& key, ORSet& out) {
    if (db == nullptr || !ensure_schema(db)) {
        return false;
    }
    out = ORSet{};

    auto counter_stmt = prepare(db, R"sql(
        SELECT site_id, counter
        FROM _sync_crdt_or_set_counters
        WHERE key = ?1;
    )sql");
    if (!counter_stmt || !bind_text(counter_stmt.get(), 1, key)) {
        return false;
    }
    while (true) {
        const auto rc = sqlite3_step(counter_stmt.get());
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            return false;
        }
        out.set_counter(
            static_cast<std::uint32_t>(sqlite3_column_int64(counter_stmt.get(), 0)),
            static_cast<std::uint64_t>(sqlite3_column_int64(counter_stmt.get(), 1))
        );
    }

    auto tag_stmt = prepare(db, R"sql(
        SELECT element, site_id, tag_counter
        FROM _sync_crdt_or_set_tags
        WHERE key = ?1;
    )sql");
    if (!tag_stmt || !bind_text(tag_stmt.get(), 1, key)) {
        return false;
    }
    while (true) {
        const auto rc = sqlite3_step(tag_stmt.get());
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            return false;
        }
        const auto* element_text = sqlite3_column_text(tag_stmt.get(), 0);
        out.add_tag(
            element_text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(element_text)),
            ORSet::Tag{
                .site_id = static_cast<std::uint32_t>(sqlite3_column_int64(tag_stmt.get(), 1)),
                .counter = static_cast<std::uint64_t>(sqlite3_column_int64(tag_stmt.get(), 2)),
            }
        );
    }

    return true;
}

} // namespace tightrope::sync::crdt
