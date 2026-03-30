-- Baseline migration: creates all tables equivalent to current Python schema head.
-- This migration runs on fresh installs. Existing installs are detected by
-- checking for the schema_version table.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    email TEXT,
    provider TEXT,
    chatgpt_account_id TEXT,
    plan_type TEXT,
    access_token_encrypted BLOB,
    refresh_token_encrypted BLOB,
    id_token_encrypted BLOB,
    last_refresh TEXT,
    deactivation_reason TEXT,
    status TEXT NOT NULL DEFAULT 'active',
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS usage_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    model TEXT NOT NULL,
    input_tokens INTEGER NOT NULL DEFAULT 0,
    output_tokens INTEGER NOT NULL DEFAULT 0,
    total_cost REAL NOT NULL DEFAULT 0,
    requested_at TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS additional_usage_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    quota_key TEXT NOT NULL,
    used_amount REAL NOT NULL DEFAULT 0,
    requested_at TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS request_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER,
    path TEXT NOT NULL,
    method TEXT NOT NULL,
    status_code INTEGER NOT NULL,
    model TEXT,
    requested_at TEXT NOT NULL DEFAULT (datetime('now')),
    error_code TEXT,
    transport TEXT,
    total_cost REAL NOT NULL DEFAULT 0,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS sticky_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_key TEXT NOT NULL,
    session_kind TEXT NOT NULL DEFAULT 'session',
    account_id INTEGER NOT NULL,
    expires_at TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(session_key, session_kind),
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS proxy_sticky_sessions (
    session_key TEXT PRIMARY KEY,
    account_id TEXT NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    expires_at_ms INTEGER NOT NULL
);

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

CREATE TABLE IF NOT EXISTS api_firewall_allowlist (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ip TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS api_keys (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    key_id TEXT NOT NULL UNIQUE,
    key_hash TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'active',
    expires_at TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS api_key_limits (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    api_key_id INTEGER NOT NULL,
    limit_type TEXT NOT NULL,
    limit_value REAL NOT NULL,
    period_seconds INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE(api_key_id, limit_type, period_seconds),
    FOREIGN KEY (api_key_id) REFERENCES api_keys(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS api_key_usage_reservations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    api_key_id INTEGER NOT NULL,
    request_id TEXT NOT NULL UNIQUE,
    status TEXT NOT NULL DEFAULT 'reserved',
    reserved_at TEXT NOT NULL DEFAULT (datetime('now')),
    settled_at TEXT,
    FOREIGN KEY (api_key_id) REFERENCES api_keys(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS api_key_usage_reservation_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    reservation_id INTEGER NOT NULL,
    metric TEXT NOT NULL,
    amount REAL NOT NULL DEFAULT 0,
    FOREIGN KEY (reservation_id) REFERENCES api_key_usage_reservations(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_usage_history_account_requested_at
    ON usage_history(account_id, requested_at);

CREATE INDEX IF NOT EXISTS idx_request_logs_requested_at
    ON request_logs(requested_at);

CREATE INDEX IF NOT EXISTS idx_request_logs_account_requested_at
    ON request_logs(account_id, requested_at);

CREATE INDEX IF NOT EXISTS idx_sticky_sessions_expires_at
    ON sticky_sessions(expires_at);

CREATE INDEX IF NOT EXISTS idx_proxy_sticky_sessions_expires_at
    ON proxy_sticky_sessions(expires_at_ms);

CREATE INDEX IF NOT EXISTS idx_api_key_reservations_api_key
    ON api_key_usage_reservations(api_key_id, status);

INSERT OR IGNORE INTO schema_version(version) VALUES (1);
INSERT OR IGNORE INTO dashboard_settings(id) VALUES (1);
