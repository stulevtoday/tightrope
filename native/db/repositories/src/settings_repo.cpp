#include "settings_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "sqlite_repo_utils.h"

namespace tightrope::db {

namespace {

constexpr const char* kCreateSettingsSql = R"SQL(
CREATE TABLE IF NOT EXISTS dashboard_settings (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    dashboard_theme TEXT NOT NULL DEFAULT 'auto',
    connect_address TEXT,
    routing_strategy TEXT NOT NULL DEFAULT 'usage_weighted',
    dashboard_password_hash TEXT,
    dashboard_totp_secret TEXT,
    sticky_threads_enabled INTEGER NOT NULL DEFAULT 0,
    upstream_stream_transport TEXT NOT NULL DEFAULT 'default',
    prefer_earlier_reset_accounts INTEGER NOT NULL DEFAULT 0,
    openai_cache_affinity_max_age_seconds INTEGER NOT NULL DEFAULT 300,
    import_without_overwrite INTEGER NOT NULL DEFAULT 0,
    totp_required_on_login INTEGER NOT NULL DEFAULT 0,
    api_key_auth_enabled INTEGER NOT NULL DEFAULT 0,
    routing_headroom_weight_primary REAL NOT NULL DEFAULT 0.35,
    routing_headroom_weight_secondary REAL NOT NULL DEFAULT 0.65,
    routing_score_alpha REAL NOT NULL DEFAULT 0.3,
    routing_score_beta REAL NOT NULL DEFAULT 0.25,
    routing_score_gamma REAL NOT NULL DEFAULT 0.2,
    routing_score_delta REAL NOT NULL DEFAULT 0.2,
    routing_score_zeta REAL NOT NULL DEFAULT 0.05,
    routing_score_eta REAL NOT NULL DEFAULT 1.0,
    routing_success_rate_rho REAL NOT NULL DEFAULT 2.0,
    sync_cluster_name TEXT NOT NULL DEFAULT 'default',
    sync_site_id INTEGER NOT NULL DEFAULT 1,
    sync_port INTEGER NOT NULL DEFAULT 9400,
    sync_discovery_enabled INTEGER NOT NULL DEFAULT 1,
    sync_interval_seconds INTEGER NOT NULL DEFAULT 5,
    sync_conflict_resolution TEXT NOT NULL DEFAULT 'lww',
    sync_journal_retention_days INTEGER NOT NULL DEFAULT 30,
    sync_tls_enabled INTEGER NOT NULL DEFAULT 1,
    totp_last_verified_step INTEGER,
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
)SQL";

constexpr const char* kSeedSettingsSql = R"SQL(
INSERT OR IGNORE INTO dashboard_settings(id) VALUES (1);
UPDATE dashboard_settings
SET
    dashboard_theme = COALESCE(dashboard_theme, 'auto'),
    routing_strategy = COALESCE(routing_strategy, 'usage_weighted'),
    upstream_stream_transport = COALESCE(upstream_stream_transport, 'default'),
    openai_cache_affinity_max_age_seconds = COALESCE(openai_cache_affinity_max_age_seconds, 300),
    routing_headroom_weight_primary = COALESCE(routing_headroom_weight_primary, 0.35),
    routing_headroom_weight_secondary = COALESCE(routing_headroom_weight_secondary, 0.65),
    routing_score_alpha = COALESCE(routing_score_alpha, 0.3),
    routing_score_beta = COALESCE(routing_score_beta, 0.25),
    routing_score_gamma = COALESCE(routing_score_gamma, 0.2),
    routing_score_delta = COALESCE(routing_score_delta, 0.2),
    routing_score_zeta = COALESCE(routing_score_zeta, 0.05),
    routing_score_eta = COALESCE(routing_score_eta, 1.0),
    routing_success_rate_rho = COALESCE(routing_success_rate_rho, 2.0),
    sync_cluster_name = COALESCE(sync_cluster_name, 'default'),
    sync_site_id = COALESCE(sync_site_id, 1),
    sync_port = COALESCE(sync_port, 9400),
    sync_discovery_enabled = COALESCE(sync_discovery_enabled, 1),
    sync_interval_seconds = COALESCE(sync_interval_seconds, 5),
    sync_conflict_resolution = COALESCE(sync_conflict_resolution, 'lww'),
    sync_journal_retention_days = COALESCE(sync_journal_retention_days, 30),
    sync_tls_enabled = COALESCE(sync_tls_enabled, 1)
WHERE id = 1;
)SQL";

bool ensure_schema(SQLite::Database& db) noexcept {
    if (!sqlite_repo_utils::exec_sql(db, kCreateSettingsSql)) {
        return false;
    }

    if (!sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "dashboard_theme",
            "ALTER TABLE dashboard_settings ADD COLUMN dashboard_theme TEXT NOT NULL DEFAULT 'auto';"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sticky_threads_enabled",
            "ALTER TABLE dashboard_settings ADD COLUMN sticky_threads_enabled INTEGER NOT NULL DEFAULT 0;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "upstream_stream_transport",
            "ALTER TABLE dashboard_settings ADD COLUMN upstream_stream_transport TEXT NOT NULL DEFAULT 'default';"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "prefer_earlier_reset_accounts",
            "ALTER TABLE dashboard_settings ADD COLUMN prefer_earlier_reset_accounts INTEGER NOT NULL DEFAULT 0;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "openai_cache_affinity_max_age_seconds",
            "ALTER TABLE dashboard_settings ADD COLUMN openai_cache_affinity_max_age_seconds INTEGER NOT NULL DEFAULT 300;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "import_without_overwrite",
            "ALTER TABLE dashboard_settings ADD COLUMN import_without_overwrite INTEGER NOT NULL DEFAULT 0;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "totp_required_on_login",
            "ALTER TABLE dashboard_settings ADD COLUMN totp_required_on_login INTEGER NOT NULL DEFAULT 0;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "api_key_auth_enabled",
            "ALTER TABLE dashboard_settings ADD COLUMN api_key_auth_enabled INTEGER NOT NULL DEFAULT 0;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_headroom_weight_primary",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_headroom_weight_primary REAL NOT NULL DEFAULT 0.35;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_headroom_weight_secondary",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_headroom_weight_secondary REAL NOT NULL DEFAULT 0.65;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_score_alpha",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_score_alpha REAL NOT NULL DEFAULT 0.3;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_score_beta",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_score_beta REAL NOT NULL DEFAULT 0.25;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_score_gamma",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_score_gamma REAL NOT NULL DEFAULT 0.2;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_score_delta",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_score_delta REAL NOT NULL DEFAULT 0.2;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_score_zeta",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_score_zeta REAL NOT NULL DEFAULT 0.05;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_score_eta",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_score_eta REAL NOT NULL DEFAULT 1.0;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "routing_success_rate_rho",
            "ALTER TABLE dashboard_settings ADD COLUMN routing_success_rate_rho REAL NOT NULL DEFAULT 2.0;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_cluster_name",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_cluster_name TEXT NOT NULL DEFAULT 'default';"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_site_id",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_site_id INTEGER NOT NULL DEFAULT 1;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_port",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_port INTEGER NOT NULL DEFAULT 9400;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_discovery_enabled",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_discovery_enabled INTEGER NOT NULL DEFAULT 1;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_interval_seconds",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_interval_seconds INTEGER NOT NULL DEFAULT 5;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_conflict_resolution",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_conflict_resolution TEXT NOT NULL DEFAULT 'lww';"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_journal_retention_days",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_journal_retention_days INTEGER NOT NULL DEFAULT 30;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "sync_tls_enabled",
            "ALTER TABLE dashboard_settings ADD COLUMN sync_tls_enabled INTEGER NOT NULL DEFAULT 1;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "dashboard_password_hash",
            "ALTER TABLE dashboard_settings ADD COLUMN dashboard_password_hash TEXT;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "dashboard_totp_secret",
            "ALTER TABLE dashboard_settings ADD COLUMN dashboard_totp_secret TEXT;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "totp_last_verified_step",
            "ALTER TABLE dashboard_settings ADD COLUMN totp_last_verified_step INTEGER;"
        ) ||
        !sqlite_repo_utils::ensure_column(
            db,
            "dashboard_settings",
            "connect_address",
            "ALTER TABLE dashboard_settings ADD COLUMN connect_address TEXT;"
        )) {
        return false;
    }

    return sqlite_repo_utils::exec_sql(db, kSeedSettingsSql);
}

bool to_bool(const SQLite::Statement& stmt, int index) {
    return !stmt.getColumn(index).isNull() && stmt.getColumn(index).getInt() != 0;
}

} // namespace

bool ensure_dashboard_settings_schema(sqlite3* db) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return ensure_schema(*handle.db);
}

std::optional<DashboardSettingsRecord> get_dashboard_settings(sqlite3* db) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT
    dashboard_theme,
    sticky_threads_enabled,
    upstream_stream_transport,
    prefer_earlier_reset_accounts,
    routing_strategy,
    openai_cache_affinity_max_age_seconds,
    import_without_overwrite,
    totp_required_on_login,
    api_key_auth_enabled,
    routing_headroom_weight_primary,
    routing_headroom_weight_secondary,
    routing_score_alpha,
    routing_score_beta,
    routing_score_gamma,
    routing_score_delta,
    routing_score_zeta,
    routing_score_eta,
    routing_success_rate_rho,
    sync_cluster_name,
    sync_site_id,
    sync_port,
    sync_discovery_enabled,
    sync_interval_seconds,
    sync_conflict_resolution,
    sync_journal_retention_days,
    sync_tls_enabled,
    connect_address,
    dashboard_password_hash,
    dashboard_totp_secret,
    totp_last_verified_step
FROM dashboard_settings
WHERE id = 1
LIMIT 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        if (!stmt.executeStep()) {
            return std::nullopt;
        }

        DashboardSettingsRecord record;
        if (const auto theme = sqlite_repo_utils::optional_text(stmt.getColumn(0)); theme.has_value()) {
            record.theme = *theme;
        }
        record.sticky_threads_enabled = to_bool(stmt, 1);
        if (const auto upstream = sqlite_repo_utils::optional_text(stmt.getColumn(2)); upstream.has_value()) {
            record.upstream_stream_transport = *upstream;
        }
        record.prefer_earlier_reset_accounts = to_bool(stmt, 3);
        if (const auto routing = sqlite_repo_utils::optional_text(stmt.getColumn(4)); routing.has_value()) {
            record.routing_strategy = *routing;
        }
        if (!stmt.getColumn(5).isNull()) {
            record.openai_cache_affinity_max_age_seconds = stmt.getColumn(5).getInt64();
        }
        record.import_without_overwrite = to_bool(stmt, 6);
        record.totp_required_on_login = to_bool(stmt, 7);
        record.api_key_auth_enabled = to_bool(stmt, 8);
        if (!stmt.getColumn(9).isNull()) {
            record.routing_headroom_weight_primary = stmt.getColumn(9).getDouble();
        }
        if (!stmt.getColumn(10).isNull()) {
            record.routing_headroom_weight_secondary = stmt.getColumn(10).getDouble();
        }
        if (!stmt.getColumn(11).isNull()) {
            record.routing_score_alpha = stmt.getColumn(11).getDouble();
        }
        if (!stmt.getColumn(12).isNull()) {
            record.routing_score_beta = stmt.getColumn(12).getDouble();
        }
        if (!stmt.getColumn(13).isNull()) {
            record.routing_score_gamma = stmt.getColumn(13).getDouble();
        }
        if (!stmt.getColumn(14).isNull()) {
            record.routing_score_delta = stmt.getColumn(14).getDouble();
        }
        if (!stmt.getColumn(15).isNull()) {
            record.routing_score_zeta = stmt.getColumn(15).getDouble();
        }
        if (!stmt.getColumn(16).isNull()) {
            record.routing_score_eta = stmt.getColumn(16).getDouble();
        }
        if (!stmt.getColumn(17).isNull()) {
            record.routing_success_rate_rho = stmt.getColumn(17).getDouble();
        }
        if (const auto sync_cluster_name = sqlite_repo_utils::optional_text(stmt.getColumn(18)); sync_cluster_name.has_value()) {
            record.sync_cluster_name = *sync_cluster_name;
        }
        if (!stmt.getColumn(19).isNull()) {
            record.sync_site_id = stmt.getColumn(19).getInt64();
        }
        if (!stmt.getColumn(20).isNull()) {
            record.sync_port = stmt.getColumn(20).getInt64();
        }
        record.sync_discovery_enabled = to_bool(stmt, 21);
        if (!stmt.getColumn(22).isNull()) {
            record.sync_interval_seconds = stmt.getColumn(22).getInt64();
        }
        if (const auto sync_conflict_resolution = sqlite_repo_utils::optional_text(stmt.getColumn(23));
            sync_conflict_resolution.has_value()) {
            record.sync_conflict_resolution = *sync_conflict_resolution;
        }
        if (!stmt.getColumn(24).isNull()) {
            record.sync_journal_retention_days = stmt.getColumn(24).getInt64();
        }
        record.sync_tls_enabled = to_bool(stmt, 25);
        if (const auto connect_address = sqlite_repo_utils::optional_text(stmt.getColumn(26)); connect_address.has_value()) {
            record.connect_address = *connect_address;
        }
        record.password_hash = sqlite_repo_utils::optional_text(stmt.getColumn(27));
        record.totp_secret = sqlite_repo_utils::optional_text(stmt.getColumn(28));
        record.totp_last_verified_step = sqlite_repo_utils::optional_i64(stmt.getColumn(29));
        return record;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<DashboardSettingsRecord> update_dashboard_settings(sqlite3* db, const DashboardSettingsPatch& patch) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return std::nullopt;
    }

    auto current = get_dashboard_settings(db);
    if (!current.has_value()) {
        return std::nullopt;
    }

    auto next = *current;
    if (patch.theme.has_value()) {
        next.theme = *patch.theme;
    }
    if (patch.sticky_threads_enabled.has_value()) {
        next.sticky_threads_enabled = *patch.sticky_threads_enabled;
    }
    if (patch.upstream_stream_transport.has_value()) {
        next.upstream_stream_transport = *patch.upstream_stream_transport;
    }
    if (patch.prefer_earlier_reset_accounts.has_value()) {
        next.prefer_earlier_reset_accounts = *patch.prefer_earlier_reset_accounts;
    }
    if (patch.routing_strategy.has_value()) {
        next.routing_strategy = *patch.routing_strategy;
    }
    if (patch.openai_cache_affinity_max_age_seconds.has_value()) {
        next.openai_cache_affinity_max_age_seconds = *patch.openai_cache_affinity_max_age_seconds;
    }
    if (patch.import_without_overwrite.has_value()) {
        next.import_without_overwrite = *patch.import_without_overwrite;
    }
    if (patch.totp_required_on_login.has_value()) {
        next.totp_required_on_login = *patch.totp_required_on_login;
    }
    if (patch.api_key_auth_enabled.has_value()) {
        next.api_key_auth_enabled = *patch.api_key_auth_enabled;
    }
    if (patch.routing_headroom_weight_primary.has_value()) {
        next.routing_headroom_weight_primary = *patch.routing_headroom_weight_primary;
    }
    if (patch.routing_headroom_weight_secondary.has_value()) {
        next.routing_headroom_weight_secondary = *patch.routing_headroom_weight_secondary;
    }
    if (patch.routing_score_alpha.has_value()) {
        next.routing_score_alpha = *patch.routing_score_alpha;
    }
    if (patch.routing_score_beta.has_value()) {
        next.routing_score_beta = *patch.routing_score_beta;
    }
    if (patch.routing_score_gamma.has_value()) {
        next.routing_score_gamma = *patch.routing_score_gamma;
    }
    if (patch.routing_score_delta.has_value()) {
        next.routing_score_delta = *patch.routing_score_delta;
    }
    if (patch.routing_score_zeta.has_value()) {
        next.routing_score_zeta = *patch.routing_score_zeta;
    }
    if (patch.routing_score_eta.has_value()) {
        next.routing_score_eta = *patch.routing_score_eta;
    }
    if (patch.routing_success_rate_rho.has_value()) {
        next.routing_success_rate_rho = *patch.routing_success_rate_rho;
    }
    if (patch.sync_cluster_name.has_value()) {
        next.sync_cluster_name = *patch.sync_cluster_name;
    }
    if (patch.sync_site_id.has_value()) {
        next.sync_site_id = *patch.sync_site_id;
    }
    if (patch.sync_port.has_value()) {
        next.sync_port = *patch.sync_port;
    }
    if (patch.sync_discovery_enabled.has_value()) {
        next.sync_discovery_enabled = *patch.sync_discovery_enabled;
    }
    if (patch.sync_interval_seconds.has_value()) {
        next.sync_interval_seconds = *patch.sync_interval_seconds;
    }
    if (patch.sync_conflict_resolution.has_value()) {
        next.sync_conflict_resolution = *patch.sync_conflict_resolution;
    }
    if (patch.sync_journal_retention_days.has_value()) {
        next.sync_journal_retention_days = *patch.sync_journal_retention_days;
    }
    if (patch.sync_tls_enabled.has_value()) {
        next.sync_tls_enabled = *patch.sync_tls_enabled;
    }
    if (patch.connect_address.has_value()) {
        next.connect_address = *patch.connect_address;
    }

    constexpr const char* kSql = R"SQL(
UPDATE dashboard_settings
SET
    dashboard_theme = ?1,
    sticky_threads_enabled = ?2,
    upstream_stream_transport = ?3,
    prefer_earlier_reset_accounts = ?4,
    routing_strategy = ?5,
    openai_cache_affinity_max_age_seconds = ?6,
    import_without_overwrite = ?7,
    totp_required_on_login = ?8,
    api_key_auth_enabled = ?9,
    routing_headroom_weight_primary = ?10,
    routing_headroom_weight_secondary = ?11,
    routing_score_alpha = ?12,
    routing_score_beta = ?13,
    routing_score_gamma = ?14,
    routing_score_delta = ?15,
    routing_score_zeta = ?16,
    routing_score_eta = ?17,
    routing_success_rate_rho = ?18,
    sync_cluster_name = ?19,
    sync_site_id = ?20,
    sync_port = ?21,
    sync_discovery_enabled = ?22,
    sync_interval_seconds = ?23,
    sync_conflict_resolution = ?24,
    sync_journal_retention_days = ?25,
    sync_tls_enabled = ?26,
    connect_address = ?27,
    dashboard_password_hash = ?28,
    dashboard_totp_secret = ?29,
    totp_last_verified_step = ?30,
    updated_at = datetime('now')
WHERE id = 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, next.theme);
        stmt.bind(2, next.sticky_threads_enabled ? 1 : 0);
        stmt.bind(3, next.upstream_stream_transport);
        stmt.bind(4, next.prefer_earlier_reset_accounts ? 1 : 0);
        stmt.bind(5, next.routing_strategy);
        stmt.bind(6, next.openai_cache_affinity_max_age_seconds);
        stmt.bind(7, next.import_without_overwrite ? 1 : 0);
        stmt.bind(8, next.totp_required_on_login ? 1 : 0);
        stmt.bind(9, next.api_key_auth_enabled ? 1 : 0);
        stmt.bind(10, next.routing_headroom_weight_primary);
        stmt.bind(11, next.routing_headroom_weight_secondary);
        stmt.bind(12, next.routing_score_alpha);
        stmt.bind(13, next.routing_score_beta);
        stmt.bind(14, next.routing_score_gamma);
        stmt.bind(15, next.routing_score_delta);
        stmt.bind(16, next.routing_score_zeta);
        stmt.bind(17, next.routing_score_eta);
        stmt.bind(18, next.routing_success_rate_rho);
        stmt.bind(19, next.sync_cluster_name);
        stmt.bind(20, next.sync_site_id);
        stmt.bind(21, next.sync_port);
        stmt.bind(22, next.sync_discovery_enabled ? 1 : 0);
        stmt.bind(23, next.sync_interval_seconds);
        stmt.bind(24, next.sync_conflict_resolution);
        stmt.bind(25, next.sync_journal_retention_days);
        stmt.bind(26, next.sync_tls_enabled ? 1 : 0);
        stmt.bind(27, next.connect_address);
        if (!sqlite_repo_utils::bind_optional_text(stmt, 28, next.password_hash) ||
            !sqlite_repo_utils::bind_optional_text(stmt, 29, next.totp_secret)) {
            return std::nullopt;
        }
        if (!next.totp_last_verified_step.has_value()) {
            stmt.bind(30);
        } else {
            stmt.bind(30, *next.totp_last_verified_step);
        }
        (void)stmt.exec();
    } catch (...) {
        return std::nullopt;
    }

    return get_dashboard_settings(db);
}

bool set_dashboard_password_hash(sqlite3* db, const std::optional<std::string_view>& password_hash) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE dashboard_settings
SET dashboard_password_hash = ?1, updated_at = datetime('now')
WHERE id = 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        if (password_hash.has_value()) {
            stmt.bind(1, std::string(password_hash->data(), password_hash->size()));
        } else {
            stmt.bind(1);
        }
        (void)stmt.exec();
        return true;
    } catch (...) {
        return false;
    }
}

bool set_dashboard_totp_secret(sqlite3* db, const std::optional<std::string_view>& totp_secret) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE dashboard_settings
SET
    dashboard_totp_secret = ?1,
    totp_last_verified_step = NULL,
    totp_required_on_login = CASE WHEN ?1 IS NULL THEN 0 ELSE totp_required_on_login END,
    updated_at = datetime('now')
WHERE id = 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        if (totp_secret.has_value()) {
            stmt.bind(1, std::string(totp_secret->data(), totp_secret->size()));
        } else {
            stmt.bind(1);
        }
        (void)stmt.exec();
        return true;
    } catch (...) {
        return false;
    }
}

bool advance_dashboard_totp_last_verified_step(sqlite3* db, const std::int64_t step) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE dashboard_settings
SET totp_last_verified_step = ?1, updated_at = datetime('now')
WHERE id = 1
  AND (totp_last_verified_step IS NULL OR totp_last_verified_step < ?1);
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, step);
        (void)stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

} // namespace tightrope::db
