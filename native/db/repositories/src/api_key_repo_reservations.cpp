#include "api_key_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api_key_repo_internal.h"
#include "api_keys/reservation.h"
#include "sqlite_repo_utils.h"

namespace tightrope::db {

std::optional<ApiKeyUsageReservationRecord> create_api_key_usage_reservation(
    sqlite3* db,
    std::string_view key_id,
    std::string_view request_id,
    const std::vector<ApiKeyUsageReservationItemInput>& items
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_api_key_schema(db) || request_id.empty()) {
        return std::nullopt;
    }

    const auto api_key_pk = api_key_repo_internal::find_api_key_pk(*handle.db, key_id);
    if (!api_key_pk.has_value()) {
        return std::nullopt;
    }

    constexpr const char* kReservationSql =
        "INSERT INTO api_key_usage_reservations(api_key_id, request_id, status) VALUES(?1, ?2, 'reserved');";
    constexpr const char* kItemSql =
        "INSERT INTO api_key_usage_reservation_items(reservation_id, metric, amount) VALUES(?1, ?2, ?3);";

    try {
        SQLite::Transaction txn(*handle.db);

        SQLite::Statement reservation_stmt(*handle.db, kReservationSql);
        reservation_stmt.bind(1, *api_key_pk);
        reservation_stmt.bind(2, std::string(request_id));
        (void)reservation_stmt.exec();

        const auto reservation_pk = handle.db->getLastInsertRowid();
        SQLite::Statement item_stmt(*handle.db, kItemSql);
        for (const auto& item : items) {
            item_stmt.reset();
            item_stmt.clearBindings();
            item_stmt.bind(1, reservation_pk);
            item_stmt.bind(2, item.metric);
            item_stmt.bind(3, item.amount);
            (void)item_stmt.exec();
        }

        txn.commit();
    } catch (...) {
        return std::nullopt;
    }

    return get_api_key_usage_reservation(db, request_id);
}

std::optional<ApiKeyUsageReservationRecord>
get_api_key_usage_reservation(sqlite3* db, std::string_view request_id) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_api_key_schema(db) || request_id.empty()) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT r.id, r.api_key_id, k.key_id, r.request_id, r.status, r.reserved_at, r.settled_at
FROM api_key_usage_reservations r
JOIN api_keys k ON k.id = r.api_key_id
WHERE r.request_id = ?1
LIMIT 1;
)SQL";
    constexpr const char* kItemsSql =
        "SELECT id, metric, amount FROM api_key_usage_reservation_items WHERE reservation_id = ?1 ORDER BY id ASC;";

    ApiKeyUsageReservationRecord record;
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(request_id));
        if (!stmt.executeStep()) {
            return std::nullopt;
        }

        record.id = stmt.getColumn(0).getInt64();
        record.api_key_id = stmt.getColumn(1).getInt64();
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(2)); text.has_value()) {
            record.key_id = *text;
        }
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(3)); text.has_value()) {
            record.request_id = *text;
        }
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(4)); text.has_value()) {
            record.status = *text;
        }
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(5)); text.has_value()) {
            record.reserved_at = *text;
        }
        record.settled_at = sqlite_repo_utils::optional_text(stmt.getColumn(6));

        SQLite::Statement item_stmt(*handle.db, kItemsSql);
        item_stmt.bind(1, record.id);
        while (item_stmt.executeStep()) {
            ApiKeyUsageReservationItemRecord item;
            item.id = item_stmt.getColumn(0).getInt64();
            if (const auto text = sqlite_repo_utils::optional_text(item_stmt.getColumn(1)); text.has_value()) {
                item.metric = *text;
            }
            item.amount = item_stmt.getColumn(2).getDouble();
            record.items.push_back(std::move(item));
        }
    } catch (...) {
        return std::nullopt;
    }

    return record;
}

bool transition_api_key_usage_reservation_status(
    sqlite3* db,
    std::string_view request_id,
    std::string_view expected_status,
    std::string_view next_status
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_api_key_schema(db) || request_id.empty() || expected_status.empty() ||
        next_status.empty()) {
        return false;
    }
    if (!auth::api_keys::is_valid_status_transition(expected_status, next_status)) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE api_key_usage_reservations
SET
    status = ?3,
    settled_at = CASE WHEN ?3 IN ('settled', 'released', 'cancelled', 'failed') THEN datetime('now') ELSE settled_at END
WHERE request_id = ?1 AND status = ?2;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(request_id));
        stmt.bind(2, std::string(expected_status));
        stmt.bind(3, std::string(next_status));
        (void)stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

} // namespace tightrope::db

