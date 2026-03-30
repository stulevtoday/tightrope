# Proxy Compatibility Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the OpenAI/Codex-compatible proxy behavior with contract-driven parity for JSON, SSE, WebSocket, compact, transcribe, and sticky affinity.

**Architecture:** Start with pure deterministic logic first: payload normalization, model registry, error envelopes, and balancer scoring. Then add protocol handlers and route wiring driven by the frozen fixtures captured in the contract-baseline track.

**Tech Stack:** Catch2, frozen HTTP/SSE/WS fixtures, uWebSockets, SQLite, node-addon-api.

---

## Chunk 1: Deterministic Core

### Task 1: Port OpenAI Payload Normalization And Error Envelopes

**Files:**
- Modify: `native/proxy/openai/payload_normalizer.h`
- Modify: `native/proxy/openai/payload_normalizer.cpp`
- Modify: `native/proxy/openai/error_envelope.h`
- Modify: `native/proxy/openai/error_envelope.cpp`
- Modify: `native/proxy/openai/model_registry.h`
- Modify: `native/proxy/openai/model_registry.cpp`
- Create: `native/tests/contracts/openai/payload_normalizer_test.cpp`

- [x] **Step 1: Write the failing normalization test**

```cpp
TEST_CASE("responses payload normalizer preserves contract fields", "[proxy][normalize]") {
    auto input = load_http_fixture("responses_post").request.body;
    auto normalized = normalize_request(input);
    REQUIRE(normalized.contains("model"));
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[proxy][normalize]"
```
Expected: FAIL.

- [x] **Step 3: Implement normalization, errors, and model lookups**

Use the frozen fixtures from `native/tests/contracts/fixtures/http/` as the canonical oracle.
Implemented with codex-lb parity constraints:
- Normalize OpenAI-compatible request aliases (`promptCacheKey` -> `prompt_cache_key`).
- Normalize `service_tier: "fast"` to `service_tier: "priority"`.
- Strip unsupported advisory parameters (`temperature`, `prompt_cache_retention`, `max_output_tokens`, `safety_identifier`) from forwarded payload.
- Preserve client-provided model in payload normalization and keep proxy model registry default-empty (no hardcoded model selection in proxy layer).
- Add OpenAI-shaped error envelope helper and contract assertions.

- [x] **Step 4: Re-run the normalization test**

Run:
```bash
./build-debug/tightrope-tests "[proxy][normalize]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/proxy/openai/payload_normalizer.h native/proxy/openai/payload_normalizer.cpp native/proxy/openai/error_envelope.h native/proxy/openai/error_envelope.cpp native/proxy/openai/model_registry.h native/proxy/openai/model_registry.cpp native/tests/contracts/openai/payload_normalizer_test.cpp
git commit -m "feat: port proxy normalization core"
```

### Task 2: Port Balancer Eligibility And Strategy Logic

**Files:**
- Modify: `native/balancer/eligibility.h`
- Modify: `native/balancer/eligibility.cpp`
- Modify: `native/balancer/scorer.h`
- Modify: `native/balancer/scorer.cpp`
- Modify: `native/balancer/strategies/round_robin.h`
- Modify: `native/balancer/strategies/round_robin.cpp`
- Modify: `native/balancer/strategies/weighted_round_robin.h`
- Modify: `native/balancer/strategies/weighted_round_robin.cpp`
- Modify: `native/balancer/strategies/power_of_two.h`
- Modify: `native/balancer/strategies/power_of_two.cpp`
- Create: `native/tests/unit/balancer/balancer_strategy_test.cpp`

- [x] **Step 1: Write the failing balancer test**

```cpp
TEST_CASE("round robin rotates across eligible accounts", "[proxy][balancer]") {
    auto accounts = sample_accounts();
    REQUIRE(pick_round_robin(accounts).id == "a1");
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[proxy][balancer]"
```
Expected: FAIL.

- [x] **Step 3: Implement eligibility and the parity-required strategies**

Minimum required first:

- `round_robin`
- `usage_weighted` compatibility
- composite score scaffolding
- `power_of_two_choices`
Implemented details:
- Shared clamp utility moved to `native/core/math/clamp.h` (single source of clamp behavior for reuse).
- Added typed candidate model + eligibility filters.
- Added composite scoring utility and deterministic best-score selector.
- Added round-robin cursor-based selection for eligible accounts.
- Added smooth weighted round-robin picker using usage-derived weights.
- Added power-of-two random sampling with score-based winner selection.
- Added `least_outstanding_requests`, `latency_ewma`, `success_rate_weighted`, `headroom_score`, `cost_aware`, and `deadline_aware` strategy implementations.
- Added shared generic selection helper `native/core/algorithm/pick.h` to avoid duplicated min/max candidate selection loops.
- Added unit coverage in `[proxy][balancer]`.

- [x] **Step 4: Re-run the balancer test**

Run:
```bash
./build-debug/tightrope-tests "[proxy][balancer]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/balancer native/tests/unit/balancer/balancer_strategy_test.cpp
git commit -m "feat: port balancer strategy core"
```

## Chunk 2: Protocol Handlers And Route Wiring

### Task 3: Port JSON, SSE, Compact, And Transcribe Endpoints

**Files:**
- Modify: `native/proxy/proxy_service.h`
- Modify: `native/proxy/proxy_service.cpp`
- Modify: `native/proxy/stream/sse_handler.h`
- Modify: `native/proxy/stream/sse_handler.cpp`
- Modify: `native/proxy/stream/compact_handler.h`
- Modify: `native/proxy/stream/compact_handler.cpp`
- Modify: `native/proxy/stream/transcribe_handler.h`
- Modify: `native/proxy/stream/transcribe_handler.cpp`
- Modify: `native/server/controllers/proxy_controller.h`
- Modify: `native/server/controllers/proxy_controller.cpp`
- Create: `native/tests/integration/proxy/proxy_http_test.cpp`
- Create: `native/tests/integration/streaming/sse_proxy_test.cpp`

- [x] **Step 1: Write the failing proxy HTTP/SSE tests**

```cpp
TEST_CASE("responses endpoint replays the frozen JSON contract", "[proxy][http]") {
    auto app = start_test_app();
    auto response = app.post_json("/v1/responses", load_http_fixture("responses_post").request.body);
    REQUIRE(response.status == load_http_fixture("responses_post").response.status);
}
```

```cpp
TEST_CASE("sse handler matches the golden transcript", "[proxy][sse]") {
    auto transcript = stream_sse("/v1/responses", load_http_fixture("responses_sse").request.body);
    REQUIRE(transcript == load_stream_fixture("responses_sse"));
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run:
```bash
./build-debug/tightrope-tests "[proxy][http]"
./build-debug/tightrope-tests "[proxy][sse]"
```
Expected: FAIL.

- [x] **Step 3: Implement HTTP and SSE protocol handling**

Cover:

- `/backend-api/codex/responses`
- `/v1/responses`
- compact endpoints
- transcribe endpoints
- models endpoints
Implemented in this slice:
- Added minimal proxy controller response types and route entry points for JSON and SSE.
- Added proxy service JSON collect + SSE stream methods with OpenAI payload normalization and OpenAI error envelope handling.
- Added deterministic SSE transcript builder for `/backend-api/codex/responses` and `/v1/responses` success/failure flows matching frozen contract event shape.
- Added integration tests for JSON route contract and SSE transcript replay.
- Added compact endpoint wiring for `/backend-api/codex/responses/compact` and `/v1/responses/compact`.
- Added transcribe endpoint wiring for `/backend-api/transcribe` and `/v1/audio/transcriptions`, including strict v1 transcription model validation (`gpt-4o-transcribe`) and OpenAI-style invalid-request envelope on mismatch.
- Added models endpoint wiring for `/api/models`, `/v1/models`, and `/backend-api/codex/models` with codex-lb-compatible response shapes.
- Removed branded hardcoded response payload text/IDs from proxy JSON, compact, and transcribe success bodies.
- Normalized SSE/WS contract transcript identifiers to neutral `proxy_*` values (no app-branded response identifiers).
- Added shared provider communication transform module (`native/proxy/openai/provider_contract.*`) for codex-lb parity:
  - SSE event alias normalization (`response.text.delta` -> `response.output_text.delta`, etc.)
  - SSE data-line/event-block normalization behavior
  - websocket `response.create` payload shaping with excluded fields (`background`, `stream`)
- Added contract tests in `native/tests/contracts/openai/provider_contract_test.cpp` and wired WS helper payload building through shared transform path.
- Added shared upstream header contract module (`native/proxy/openai/upstream_headers.*`) mirroring codex-lb rules:
  - inbound header drop/filter rules for proxy forwarding
  - standard upstream JSON/SSE header construction
  - transcribe-specific minimal forwarded header set
  - websocket upstream header construction with hop-by-hop + `Connection` token stripping
- Added contract tests in `native/tests/contracts/openai/upstream_headers_test.cpp`.
- Added dedicated compact payload normalization path (`normalize_compact_request`) and wired compact endpoint validation to it:
  - strips `store` for compact upstream payload parity
  - preserves compact alias normalization + unsupported field stripping
- Added shared upstream request plan module (`native/proxy/openai/upstream_request_plan.*`) for provider communication:
  - responses HTTP plan (`/codex/responses`, SSE headers, normalized payload)
  - responses websocket plan (`/codex/responses`, websocket headers, `response.create` payload)
  - responses stream plan resolver (`default/auto/http/websocket`) matching codex-lb transport precedence
  - compact HTTP plan (`/codex/responses/compact`, JSON headers, compact-normalized payload)
  - transcribe HTTP plan (`/transcribe`, minimal transcribe header forwarding, multipart payload summary contract)
- Added contract tests in `native/tests/contracts/openai/upstream_request_plan_test.cpp`.
- Added transport resolver parity module (`native/proxy/openai/stream_transport.*`) and tests:
  - native Codex header detection (`originator`, `x-codex-turn-state`, `x-codex-turn-metadata`, `x-codex-beta-features`)
  - model `prefer_websockets` selection fallback
  - explicit `http` / `websocket` transport forcing and override precedence
- Added shared ASCII text helpers (`native/core/text/ascii.h`) and reused them across proxy OpenAI modules to remove duplicated helper functions.
- Expanded model registry parity (`native/proxy/openai/model_registry.*`):
  - case-insensitive model lookup
  - bootstrap websocket preference fallback for `gpt-5.4` and `gpt-5.4-*`
  - explicit model preference overrides bootstrap fallback
- Wired inbound header forwarding through proxy controller/service APIs so request-plan parity logic receives real client header context.
- Added integration coverage in `native/tests/integration/proxy/proxy_upstream_transport_test.cpp` asserting:
  - filtered header forwarding to upstream plans
  - native Codex headers trigger websocket request-plan transport selection for SSE route handling.
- Added runtime websocket proxy execution module (`native/proxy/ws_proxy.*`) and controller entry-point:
  - upstream websocket request-plan execution through shared transport abstraction
  - websocket transcript failure mapping (`invalid_request_error`, `upstream_error`, `not_found`)
  - provider payload alias normalization for upstream websocket events
  - explicit `stream_incomplete` failure mapping when accepted upstream websocket streams close without events (no deterministic replay fallback)
- Added websocket transport integration tests in `native/tests/integration/proxy/proxy_upstream_transport_test.cpp` validating:
  - websocket request-plan transport/path/payload/header contracts
  - invalid websocket payload pre-validation (no upstream call)
  - upstream websocket failure close/error transcript behavior
  - accepted websocket stream + empty event transcript maps to `stream_incomplete`.

- [x] **Step 4: Re-run the HTTP and SSE tests**

Run:
```bash
./build-debug/tightrope-tests "[proxy][http]"
./build-debug/tightrope-tests "[proxy][sse]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/proxy/proxy_service.h native/proxy/proxy_service.cpp native/proxy/stream/sse_handler.h native/proxy/stream/sse_handler.cpp native/proxy/stream/compact_handler.h native/proxy/stream/compact_handler.cpp native/proxy/stream/transcribe_handler.h native/proxy/stream/transcribe_handler.cpp native/server/controllers/proxy_controller.h native/server/controllers/proxy_controller.cpp native/tests/integration/proxy/proxy_http_test.cpp native/tests/integration/streaming/sse_proxy_test.cpp
git commit -m "feat: port proxy http and sse compatibility"
```

### Task 4: Port WebSocket And Sticky Affinity Runtime Behavior

**Files:**
- Modify: `native/proxy/stream/ws_handler.h`
- Modify: `native/proxy/stream/ws_handler.cpp`
- Modify: `native/proxy/session/sticky_resolver.h`
- Modify: `native/proxy/session/sticky_resolver.cpp`
- Modify: `native/proxy/session/session_bridge.h`
- Modify: `native/proxy/session/session_bridge.cpp`
- Modify: `native/proxy/session/session_cleanup.h`
- Modify: `native/proxy/session/session_cleanup.cpp`
- Create: `native/tests/integration/streaming/ws_proxy_test.cpp`
- Create: `native/tests/integration/proxy/sticky_affinity_test.cpp`

- [x] **Step 1: Write the failing WebSocket/affinity tests**

```cpp
TEST_CASE("websocket proxy preserves turn state semantics", "[proxy][ws]") {
    auto transcript = stream_ws("/v1/responses", load_stream_fixture("responses_ws"));
    REQUIRE(transcript.accepted);
}
```

```cpp
TEST_CASE("sticky resolver reuses the same upstream account for a session key", "[proxy][sticky]") {
    auto resolver = make_test_sticky_resolver();
    auto first = resolver.pick("session-1");
    auto second = resolver.pick("session-1");
    REQUIRE(first.account_id == second.account_id);
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run:
```bash
./build-debug/tightrope-tests "[proxy][ws]"
./build-debug/tightrope-tests "[proxy][sticky]"
```
Expected: FAIL.

- [x] **Step 3: Implement WebSocket turn-state handling and sticky persistence**

Preserve:

- accept semantics
- turn state headers/fields
- session/thread/prompt-cache affinity keys
- cleanup scheduler
Implemented in this slice:
- Added deterministic WebSocket contract replay helper for `/v1/responses` and `/backend-api/codex/responses`.
- Added sticky resolver with key-to-account persistence and deterministic account assignment for new keys.
- Added integration tests for WS transcript replay and sticky key reuse behavior.
- Added session bridge upsert/find/purge lifecycle behavior with TTL-based staleness rules.
- Added stale-session cleanup scheduler with interval gating and bridge purge execution.

- [x] **Step 4: Re-run the WebSocket/affinity tests**

Run:
```bash
./build-debug/tightrope-tests "[proxy][ws]"
./build-debug/tightrope-tests "[proxy][sticky]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/proxy/stream/ws_handler.h native/proxy/stream/ws_handler.cpp native/proxy/session/sticky_resolver.h native/proxy/session/sticky_resolver.cpp native/proxy/session/session_bridge.h native/proxy/session/session_bridge.cpp native/proxy/session/session_cleanup.h native/proxy/session/session_cleanup.cpp native/tests/integration/streaming/ws_proxy_test.cpp native/tests/integration/proxy/sticky_affinity_test.cpp
git commit -m "feat: port websocket and sticky affinity behavior"
```
