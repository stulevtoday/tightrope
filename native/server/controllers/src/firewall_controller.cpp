#include "firewall_controller.h"

#include "controller_db.h"
#include "repositories/firewall_repo.h"

namespace tightrope::server::controllers {

namespace {

FirewallMutationResponse db_unavailable_mutation() {
    return {
        .status = 500,
        .code = "db_unavailable",
        .message = "Database unavailable",
    };
}

FirewallDeleteResponse db_unavailable_delete() {
    return {
        .status = 500,
        .code = "db_unavailable",
        .message = "Database unavailable",
    };
}

} // namespace

FirewallListResponse list_firewall_ips(sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    FirewallListResponse response;
    response.status = 200;
    const auto rows = db::list_firewall_allowlist_entries(handle.db);
    response.mode = rows.empty() ? "allow_all" : "allowlist_active";
    response.entries.reserve(rows.size());
    for (const auto& row : rows) {
        response.entries.push_back({
            .ip_address = row.ip_address,
            .created_at = row.created_at,
        });
    }
    return response;
}

FirewallMutationResponse add_firewall_ip(const std::string_view ip_address, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return db_unavailable_mutation();
    }

    db::FirewallIpRecord created;
    const auto result = db::add_firewall_allowlist_ip(handle.db, ip_address, &created);
    switch (result) {
    case db::FirewallWriteResult::Created:
        return {
            .status = 200,
            .entry =
                {
                    .ip_address = created.ip_address,
                    .created_at = created.created_at,
                },
        };
    case db::FirewallWriteResult::InvalidIp:
        return {
            .status = 400,
            .code = "invalid_ip",
            .message = "Invalid IP address",
        };
    case db::FirewallWriteResult::Duplicate:
        return {
            .status = 409,
            .code = "ip_exists",
            .message = "IP address already exists",
        };
    case db::FirewallWriteResult::Deleted:
    case db::FirewallWriteResult::NotFound:
    case db::FirewallWriteResult::Error:
    default:
        return {
            .status = 500,
            .code = "firewall_create_failed",
            .message = "Failed to create firewall allowlist entry",
        };
    }
}

FirewallDeleteResponse delete_firewall_ip(const std::string_view ip_address, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return db_unavailable_delete();
    }

    const auto result = db::delete_firewall_allowlist_ip(handle.db, ip_address);
    switch (result) {
    case db::FirewallWriteResult::Deleted:
        return {
            .status = 200,
            .result = "deleted",
        };
    case db::FirewallWriteResult::InvalidIp:
        return {
            .status = 400,
            .code = "invalid_ip",
            .message = "Invalid IP address",
        };
    case db::FirewallWriteResult::NotFound:
        return {
            .status = 404,
            .code = "ip_not_found",
            .message = "IP address not found",
        };
    case db::FirewallWriteResult::Created:
    case db::FirewallWriteResult::Duplicate:
    case db::FirewallWriteResult::Error:
    default:
        return {
            .status = 500,
            .code = "firewall_delete_failed",
            .message = "Failed to delete firewall allowlist entry",
        };
    }
}

} // namespace tightrope::server::controllers
