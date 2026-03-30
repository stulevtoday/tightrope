# Admin Control Plane Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the deterministic dashboard and management APIs before the proxy path so the desktop shell can validate parity through stable CRUD and auth workflows.

**Architecture:** Keep controllers thin, put SQL in repositories, and isolate auth logic in `native/auth/`. Start with `settings` and `dashboard_auth`, then add `api_keys`, `accounts`, `request_logs`, `usage`, `firewall`, and sticky-session admin CRUD.

**Tech Stack:** Catch2, SQLite3, SQLiteCpp, node-addon-api bridge, Electron preload/native wrapper.

---

## Chunk 1: Settings And Dashboard Auth

### Task 1: Port Settings Repository And Controller

**Files:**
- Modify: `native/core/types/settings.h`
- Modify: `native/db/repositories/settings_repo.h`
- Modify: `native/db/repositories/settings_repo.cpp`
- Modify: `native/server/controllers/settings_controller.h`
- Modify: `native/server/controllers/settings_controller.cpp`
- Create: `native/tests/integration/api/settings_api_test.cpp`

- [ ] **Step 1: Write the failing settings API test**

```cpp
TEST_CASE("settings api round-trips runtime settings", "[admin][settings]") {
    auto app = start_test_app();
    auto response = app.put_json("/api/settings", R"({"connect_address":"127.0.0.1:2455"})");
    REQUIRE(response.status == 200);
    REQUIRE(app.get_json("/api/settings")["connect_address"] == "127.0.0.1:2455");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[admin][settings]"
```
Expected: FAIL because repository/controller behavior is missing.

- [ ] **Step 3: Implement minimal repo and controller behavior**

Cover:

- `GET /api/settings`
- `PUT /api/settings`
- `GET /api/settings/runtime/connect-address`

- [ ] **Step 4: Re-run the settings API test**

Run:
```bash
./build-debug/tightrope-tests "[admin][settings]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/core/types/settings.h native/db/repositories/settings_repo.h native/db/repositories/settings_repo.cpp native/server/controllers/settings_controller.h native/server/controllers/settings_controller.cpp native/tests/integration/api/settings_api_test.cpp
git commit -m "feat: port settings api"
```

### Task 2: Port Dashboard Password And TOTP Auth

**Files:**
- Modify: `native/auth/dashboard/password_auth.h`
- Modify: `native/auth/dashboard/password_auth.cpp`
- Modify: `native/auth/dashboard/totp_auth.h`
- Modify: `native/auth/dashboard/totp_auth.cpp`
- Modify: `native/auth/dashboard/session_manager.h`
- Modify: `native/auth/dashboard/session_manager.cpp`
- Modify: `native/server/controllers/auth_controller.h`
- Modify: `native/server/controllers/auth_controller.cpp`
- Create: `native/tests/auth/dashboard_auth_test.cpp`

- [ ] **Step 1: Write the failing auth test**

```cpp
TEST_CASE("dashboard auth establishes a session cookie", "[admin][auth]") {
    auto app = start_test_app();
    auto response = app.post_json("/api/dashboard-auth/login", R"({"password":"secret"})");
    REQUIRE(response.status == 200);
    REQUIRE(response.has_cookie("tightrope_session"));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[admin][auth]"
```
Expected: FAIL because auth/session behavior is missing.

- [ ] **Step 3: Implement password verify, TOTP verify, and session issuance**

Preserve:

- password semantics
- optional TOTP flow
- login/logout/session-status routes

- [ ] **Step 4: Re-run the auth test**

Run:
```bash
./build-debug/tightrope-tests "[admin][auth]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/auth/dashboard/password_auth.h native/auth/dashboard/password_auth.cpp native/auth/dashboard/totp_auth.h native/auth/dashboard/totp_auth.cpp native/auth/dashboard/session_manager.h native/auth/dashboard/session_manager.cpp native/server/controllers/auth_controller.h native/server/controllers/auth_controller.cpp native/tests/auth/dashboard_auth_test.cpp
git commit -m "feat: port dashboard auth"
```

## Chunk 2: Keys, Accounts, Logs, Usage, And Admin CRUD

### Task 3: Port API Keys And Limits

**Files:**
- Modify: `native/core/types/api_key.h`
- Modify: `native/auth/api_keys/key_validator.h`
- Modify: `native/auth/api_keys/key_validator.cpp`
- Modify: `native/auth/api_keys/limit_enforcer.h`
- Modify: `native/auth/api_keys/limit_enforcer.cpp`
- Modify: `native/auth/api_keys/reservation.h`
- Modify: `native/auth/api_keys/reservation.cpp`
- Modify: `native/db/repositories/api_key_repo.h`
- Modify: `native/db/repositories/api_key_repo.cpp`
- Modify: `native/server/controllers/keys_controller.h`
- Modify: `native/server/controllers/keys_controller.cpp`
- Create: `native/tests/integration/api/api_keys_api_test.cpp`

- [ ] **Step 1: Write the failing API key test**

```cpp
TEST_CASE("api keys can be created and listed", "[admin][api-keys]") {
    auto app = start_test_app();
    auto created = app.post_json("/api/api-keys", R"({"name":"test-key"})");
    REQUIRE(created.status == 201);
    REQUIRE(app.get_json("/api/api-keys")["items"].size() == 1);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[admin][api-keys]"
```
Expected: FAIL.

- [ ] **Step 3: Implement repo, validation, and controller behavior**

Cover:

- CRUD
- limits serialization
- reservation settlement primitives

- [ ] **Step 4: Re-run the API key test**

Run:
```bash
./build-debug/tightrope-tests "[admin][api-keys]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/core/types/api_key.h native/auth/api_keys native/db/repositories/api_key_repo.h native/db/repositories/api_key_repo.cpp native/server/controllers/keys_controller.h native/server/controllers/keys_controller.cpp native/tests/integration/api/api_keys_api_test.cpp
git commit -m "feat: port api keys control plane"
```

### Task 4: Port Accounts, Logs, Usage, Firewall, And Sticky Session Admin CRUD

**Files:**
- Modify: `native/core/types/account.h`
- Modify: `native/core/types/session.h`
- Modify: `native/db/repositories/account_repo.h`
- Modify: `native/db/repositories/account_repo.cpp`
- Modify: `native/db/repositories/request_log_repo.h`
- Modify: `native/db/repositories/request_log_repo.cpp`
- Modify: `native/db/repositories/usage_repo.h`
- Modify: `native/db/repositories/usage_repo.cpp`
- Modify: `native/db/repositories/firewall_repo.h`
- Modify: `native/db/repositories/firewall_repo.cpp`
- Modify: `native/db/repositories/session_repo.h`
- Modify: `native/db/repositories/session_repo.cpp`
- Modify: `native/server/controllers/accounts_controller.h`
- Modify: `native/server/controllers/accounts_controller.cpp`
- Modify: `native/server/controllers/logs_controller.h`
- Modify: `native/server/controllers/logs_controller.cpp`
- Modify: `native/server/controllers/usage_controller.h`
- Modify: `native/server/controllers/usage_controller.cpp`
- Modify: `native/server/controllers/firewall_controller.h`
- Modify: `native/server/controllers/firewall_controller.cpp`
- Modify: `native/server/controllers/sessions_controller.h`
- Modify: `native/server/controllers/sessions_controller.cpp`
- Create: `native/tests/integration/api/admin_data_plane_test.cpp`

- [ ] **Step 1: Write the failing admin data-plane test**

```cpp
TEST_CASE("dashboard overview aggregates account and usage data", "[admin][overview]") {
    seed_account_and_usage();
    auto app = start_test_app();
    auto overview = app.get_json("/api/dashboard/overview");
    REQUIRE(overview["accounts"].size() == 1);
    REQUIRE(overview["usage"].contains("total_cost"));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[admin][overview]"
```
Expected: FAIL.

- [ ] **Step 3: Implement repositories and controllers incrementally**

Port in this order:

1. accounts
2. usage/dashboard overview
3. request logs + options
4. firewall CRUD
5. sticky session admin CRUD

- [ ] **Step 4: Re-run focused and aggregate admin tests**

Run:
```bash
./build-debug/tightrope-tests "[admin]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/core/types/account.h native/core/types/session.h native/db/repositories/account_repo.h native/db/repositories/account_repo.cpp native/db/repositories/request_log_repo.h native/db/repositories/request_log_repo.cpp native/db/repositories/usage_repo.h native/db/repositories/usage_repo.cpp native/db/repositories/firewall_repo.h native/db/repositories/firewall_repo.cpp native/db/repositories/session_repo.h native/db/repositories/session_repo.cpp native/server/controllers/accounts_controller.h native/server/controllers/accounts_controller.cpp native/server/controllers/logs_controller.h native/server/controllers/logs_controller.cpp native/server/controllers/usage_controller.h native/server/controllers/usage_controller.cpp native/server/controllers/firewall_controller.h native/server/controllers/firewall_controller.cpp native/server/controllers/sessions_controller.h native/server/controllers/sessions_controller.cpp native/tests/integration/api/admin_data_plane_test.cpp
git commit -m "feat: port admin control plane"
```
