#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "internal/token_refresh_scheduler.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "server/oauth_provider_fake.h"

namespace {

std::string make_temp_db_path() {
    const auto file =
        std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-token-refresh-scheduler.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

bool exec_sql(sqlite3* db, const char* sql) {
    if (db == nullptr || sql == nullptr) {
        return false;
    }
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool set_last_success_hours_ago(sqlite3* db, const std::string_view account_id, const int hours_ago) {
    if (db == nullptr || account_id.empty() || hours_ago < 0) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET last_refresh = datetime('now', ?1),
    token_refresh_last_success_at_ms = (CAST(strftime('%s', 'now') AS INTEGER) * 1000) - ?2,
    token_refresh_next_due_at_ms = NULL,
    token_refresh_needs_attention = 0,
    token_refresh_last_error = NULL,
    updated_at = datetime('now')
WHERE chatgpt_account_id = ?3;
)SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    const auto offset = "-" + std::to_string(hours_ago) + " hours";
    const auto age_ms = static_cast<std::int64_t>(hours_ago) * 60LL * 60LL * 1000LL;
    const auto bind_ok = sqlite3_bind_text(stmt, 1, offset.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
                         sqlite3_bind_int64(stmt, 2, age_ms) == SQLITE_OK &&
                         sqlite3_bind_text(stmt, 3, std::string(account_id).c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    if (!bind_ok) {
        finalize();
        return false;
    }
    const auto rc = sqlite3_step(stmt);
    const auto changes = sqlite3_changes(db);
    finalize();
    return rc == SQLITE_DONE && changes > 0;
}

std::optional<std::int64_t> query_i64(sqlite3* db, const std::string_view sql) {
    if (db == nullptr || sql.empty()) {
        return std::nullopt;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return std::nullopt;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
        finalize();
        return std::nullopt;
    }
    const auto value = sqlite3_column_int64(stmt, 0);
    finalize();
    return value;
}

std::optional<std::string> query_text(sqlite3* db, const std::string_view sql) {
    if (db == nullptr || sql.empty()) {
        return std::nullopt;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return std::nullopt;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
        finalize();
        return std::nullopt;
    }
    const auto* raw = sqlite3_column_text(stmt, 0);
    if (raw == nullptr) {
        finalize();
        return std::nullopt;
    }
    const auto value = std::string(reinterpret_cast<const char*>(raw));
    finalize();
    return value;
}

struct TokenProviderOverrideGuard {
    ~TokenProviderOverrideGuard() {
        tightrope::auth::oauth::clear_token_refresh_provider_for_testing();
    }
};

class FailingRefreshProvider final : public tightrope::auth::oauth::ProviderClient {
  public:
    tightrope::auth::oauth::ProviderResult<tightrope::auth::oauth::DeviceCodePayload> request_device_code() override {
        return tightrope::auth::oauth::ProviderResult<tightrope::auth::oauth::DeviceCodePayload>::fail({
            .code = "unsupported",
            .message = "device code is unsupported",
            .status_code = 0,
        });
    }

    tightrope::auth::oauth::ProviderResult<tightrope::auth::oauth::OAuthTokens>
    exchange_authorization_code(const tightrope::auth::oauth::AuthorizationCodeRequest&) override {
        return tightrope::auth::oauth::ProviderResult<tightrope::auth::oauth::OAuthTokens>::fail({
            .code = "unsupported",
            .message = "authorization code exchange is unsupported",
            .status_code = 0,
        });
    }

    tightrope::auth::oauth::ProviderResult<tightrope::auth::oauth::OAuthTokens>
    refresh_access_token(std::string_view) override {
        return tightrope::auth::oauth::ProviderResult<tightrope::auth::oauth::OAuthTokens>::fail({
            .code = "oauth_refresh_failed",
            .message = "simulated refresh failure",
            .status_code = 502,
        });
    }

    tightrope::auth::oauth::DeviceTokenPollResult
    exchange_device_token(const tightrope::auth::oauth::DeviceTokenPollRequest&) override {
        return {
            .kind = tightrope::auth::oauth::DeviceTokenPollKind::error,
            .error = tightrope::auth::oauth::OAuthError{
                .code = "unsupported",
                .message = "device token poll is unsupported",
                .status_code = 0,
            },
        };
    }
};

} // namespace

TEST_CASE("token refresh scheduler startup sweep refreshes only stale accounts", "[server][token-refresh][startup]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::ensure_accounts_schema(db));

    tightrope::db::OauthAccountUpsert stale{};
    stale.email = "stale@example.com";
    stale.provider = "openai";
    stale.chatgpt_account_id = "acc-stale";
    stale.plan_type = "plus";
    stale.access_token_encrypted = "stale-access";
    stale.refresh_token_encrypted = "stale-refresh";
    stale.id_token_encrypted = tightrope::tests::server::make_id_token("stale@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, stale).has_value());

    tightrope::db::OauthAccountUpsert fresh{};
    fresh.email = "fresh@example.com";
    fresh.provider = "openai";
    fresh.chatgpt_account_id = "acc-fresh";
    fresh.plan_type = "plus";
    fresh.access_token_encrypted = "fresh-access";
    fresh.refresh_token_encrypted = "fresh-refresh";
    fresh.id_token_encrypted = tightrope::tests::server::make_id_token("fresh@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, fresh).has_value());

    REQUIRE(set_last_success_hours_ago(db, "acc-stale", 5));
    REQUIRE(set_last_success_hours_ago(db, "acc-fresh", 1));

    TokenProviderOverrideGuard guard;
    auto provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("stale@example.com", 0);
    tightrope::auth::oauth::set_token_refresh_provider_for_testing(provider);

    const auto before = now_ms();
    const auto result = tightrope::server::internal::token_refresh::run_sweep_once(
        db,
        before,
        tightrope::server::internal::token_refresh::SchedulerConfig{},
        tightrope::server::internal::token_refresh::SweepMode::startup
    );

    REQUIRE(result.scanned == 2);
    REQUIRE(result.due == 1);
    REQUIRE(result.refreshed == 1);
    REQUIRE(result.failed == 0);
    REQUIRE(provider->refresh_exchange_calls() == 1);

    const auto stale_next_due = query_i64(
        db,
        "SELECT token_refresh_next_due_at_ms FROM accounts WHERE chatgpt_account_id = 'acc-stale' LIMIT 1;"
    );
    REQUIRE(stale_next_due.has_value());
    REQUIRE(*stale_next_due >= before + (23LL * 60LL * 60LL * 1000LL));

    const auto stale_attention = query_i64(
        db,
        "SELECT token_refresh_needs_attention FROM accounts WHERE chatgpt_account_id = 'acc-stale' LIMIT 1;"
    );
    REQUIRE(stale_attention.has_value());
    REQUIRE(*stale_attention == 0);

    const auto fresh_last_success = query_i64(
        db,
        "SELECT token_refresh_last_success_at_ms FROM accounts WHERE chatgpt_account_id = 'acc-fresh' LIMIT 1;"
    );
    REQUIRE(fresh_last_success.has_value());
    REQUIRE(*fresh_last_success <= before - (30LL * 60LL * 1000LL));

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("token refresh scheduler marks attention when refresh fails", "[server][token-refresh][attention]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::ensure_accounts_schema(db));

    tightrope::db::OauthAccountUpsert stale{};
    stale.email = "failed@example.com";
    stale.provider = "openai";
    stale.chatgpt_account_id = "acc-failed";
    stale.plan_type = "plus";
    stale.access_token_encrypted = "failed-access";
    stale.refresh_token_encrypted = "failed-refresh";
    stale.id_token_encrypted = tightrope::tests::server::make_id_token("failed@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, stale).has_value());
    REQUIRE(set_last_success_hours_ago(db, "acc-failed", 6));

    TokenProviderOverrideGuard guard;
    tightrope::auth::oauth::set_token_refresh_provider_for_testing(std::make_shared<FailingRefreshProvider>());

    tightrope::server::internal::token_refresh::SchedulerConfig config{};
    config.failure_retry_ms = 30LL * 60LL * 1000LL;
    const auto before = now_ms();
    const auto result = tightrope::server::internal::token_refresh::run_sweep_once(
        db,
        before,
        config,
        tightrope::server::internal::token_refresh::SweepMode::startup
    );

    REQUIRE(result.scanned == 1);
    REQUIRE(result.due == 1);
    REQUIRE(result.refreshed == 0);
    REQUIRE(result.failed == 1);

    const auto attention = query_i64(
        db,
        "SELECT token_refresh_needs_attention FROM accounts WHERE chatgpt_account_id = 'acc-failed' LIMIT 1;"
    );
    REQUIRE(attention.has_value());
    REQUIRE(*attention == 1);

    const auto next_due = query_i64(
        db,
        "SELECT token_refresh_next_due_at_ms FROM accounts WHERE chatgpt_account_id = 'acc-failed' LIMIT 1;"
    );
    REQUIRE(next_due.has_value());
    REQUIRE(*next_due >= before + (20LL * 60LL * 1000LL));

    const auto last_error = query_text(
        db,
        "SELECT token_refresh_last_error FROM accounts WHERE chatgpt_account_id = 'acc-failed' LIMIT 1;"
    );
    REQUIRE(last_error.has_value());
    REQUIRE(last_error->find("oauth_refresh_failed") != std::string::npos);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
