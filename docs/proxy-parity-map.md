# Tightrope Proxy Parity Map

Date: 2026-03-29
Scope: proxy-facing behavior parity against `/Users/fabian/Development/codex-lb` (source-only comparison, no Python runtime execution).

## Status Legend
- `DONE`: implemented with matching intent.
- `PARTIAL`: route/feature exists but contract behavior differs.
- `MISSING`: not implemented.

## Endpoint Parity Matrix

| Endpoint | codex-lb | tightrope | Status | Notes |
| --- | --- | --- | --- | --- |
| `POST /backend-api/codex/responses` | Streaming Responses path | Implemented | `PARTIAL` | Tightrope switches by `Accept` header; codex-lb path is stream-oriented and includes richer validation/continuity semantics. |
| `WS /backend-api/codex/responses` | WebSocket Responses | Implemented | `PARTIAL` | Tightrope WS route exists, but downstream turn-state accept/header semantics are not fully aligned. |
| `POST /v1/responses` | Stream or non-stream based on payload semantics | Implemented | `PARTIAL` | Tightrope route exists but validation/normalization/error coverage is narrower. |
| `WS /v1/responses` | WebSocket Responses | Implemented | `PARTIAL` | Route exists; advanced turn-state affinity and reconnect semantics are not complete. |
| `POST /backend-api/codex/responses/compact` | Direct compact upstream contract | Implemented | `PARTIAL` | Path exists and strips unsupported fields, but execution is still default-transport stub behavior. |
| `POST /v1/responses/compact` | Compact compatibility path | Implemented | `PARTIAL` | Same gap as backend compact path. |
| `GET /backend-api/codex/models` | Public model list | Implemented | `PARTIAL` | Route exists but uses local default registry rather than codex-lb dynamic behavior. |
| `GET /v1/models` | Public model list | Implemented | `PARTIAL` | Same model-registry parity gap. |
| `GET /api/models` | Dashboard model list | Implemented | `PARTIAL` | Route shape exists. |
| `POST /backend-api/transcribe` | Transcribe proxy path | Implemented | `PARTIAL` | Route exists but upstream execution is still default transport stub. |
| `POST /v1/audio/transcriptions` | v1 transcription path with model guard | Implemented | `PARTIAL` | Model guard exists; upstream execution parity is incomplete. |
| `POST /v1/chat/completions` | Chat-completions mapped onto Responses | Not implemented | `MISSING` | No route wiring in tightrope server routes. |
| `GET /api/codex/usage` | Rate-limit/usage payload endpoint | Not implemented | `MISSING` | No runtime route in tightrope server wiring. |

## Responses API Compatibility Requirement Map (36 Requirements)

Source: `/Users/fabian/Development/codex-lb/openspec/specs/responses-api-compat/spec.md`

| Requirement | Status | Tightrope Notes |
| --- | --- | --- |
| Validate Responses create requests | `PARTIAL` | `model/input` validation exists; full parameter conflict coverage is not complete. |
| Input types + conversation constraints | `PARTIAL` | String/array normalization exists; `conversation` and `previous_response_id` conflict handling is incomplete. |
| Reject `input_file.file_id` | `MISSING` | No explicit guard found. |
| Stream terminal completion guarantees | `PARTIAL` | WS incomplete mapping exists; full parity behavior across all stream paths is incomplete. |
| Streaming event taxonomy | `PARTIAL` | Alias support exists; full taxonomy/order/sequence parity is incomplete. |
| Non-streaming full response object | `PARTIAL` | Path exists; full OpenAI parity object behavior is incomplete. |
| Reconstruct output from item events | `MISSING` | No collector equivalent to codex-lb reconstruction path. |
| Error envelope parity | `PARTIAL` | OpenAI envelope helpers exist; code/param/type fidelity is incomplete for all branches. |
| Validate `include` values | `MISSING` | No include whitelist validation found. |
| Allow `web_search`; reject unsupported built-ins | `MISSING` | No tool-type gating parity found. |
| Preserve `service_tier` values | `PARTIAL` | `fast -> priority` aliasing exists; full policy parity not complete. |
| Inline `input_image` URLs | `MISSING` | No fetch/inline path found. |
| Reject `truncation` | `MISSING` | No explicit guard found. |
| Preserve tool call events/output items | `PARTIAL` | Basic stream forwarding exists; full tool call parity is incomplete. |
| Usage propagation | `PARTIAL` | Basic usage fields appear in stub payloads; real upstream propagation parity is incomplete. |
| Strip `safety_identifier` | `DONE` | Implemented in payload normalizer. |
| Strip advisory params (`prompt_cache_retention`, `temperature`) | `DONE` | Implemented in payload normalizer while preserving `prompt_cache_key`. |
| Prompt-cache-key affinity | `PARTIAL` | Sticky affinity exists; full settings-driven freshness and policy parity differs. |
| HTTP response session bridge continuity | `MISSING` | `SessionBridge` exists but is not wired into active proxy flow. |
| WebSocket turn-state advertise/honor | `MISSING` | No full accept/header continuity contract parity. |
| Narrow WS->HTTP fallback policy | `MISSING` | Full fallback rules are not implemented end-to-end. |
| Normalize prompt-cache aliases | `DONE` | `promptCacheKey` and `promptCacheRetention` normalization implemented. |
| Sanitize interleaved/legacy chat fields | `MISSING` | No equivalent sanitizer coverage found. |
| Preserve top-level reasoning controls | `PARTIAL` | Top-level reasoning alias normalization exists; full parity validation is incomplete. |
| Normalize assistant text part types | `MISSING` | No assistant `input_text -> output_text` conversion path found. |
| Normalize tool message history | `MISSING` | No `tool -> function_call_output` coercion path found. |
| Reject unsupported message roles | `MISSING` | No explicit role rejection path found. |
| Strip proxy identity headers before upstream | `DONE` | Forwarding headers (`Forwarded`, `X-Forwarded-*`, `CF-*`, etc.) are filtered. |
| Codex `session_id` header affinity | `MISSING` | Tightrope sticky key extraction is payload-based; header-driven affinity parity is incomplete. |
| Compact contract semantics | `PARTIAL` | Endpoint contract shape exists; robust upstream execution/retry/settlement parity is incomplete. |
| Persist request log transport | `MISSING` | No codex-lb-equivalent request-log persistence pipeline in proxy path. |
| Opt-in service-tier trace logs | `MISSING` | No config-gated service-tier trace parity found. |
| Bounded retry budget for streaming | `MISSING` | No codex-lb equivalent retry-budget path found. |
| Retry only account-recoverable streaming failures | `MISSING` | No full recoverable-failure retry policy parity found. |
| Compact latency-bound behavior parity | `MISSING` | No equivalent compact budget/timeout policy path. |
| Gated model-selection stable proxy error codes | `MISSING` | Full error-code surface parity not implemented. |

## Proxy Runtime Observability Map

Source: `/Users/fabian/Development/codex-lb/openspec/specs/proxy-runtime-observability/spec.md`

| Requirement | Status | Tightrope Notes |
| --- | --- | --- |
| Timestamped runtime logs | `DONE` | Quill logger emits timestamped lines. |
| Optional upstream request summary tracing | `PARTIAL` | Lifecycle logs exist, but not fully config-gated or fully correlated with codex-lb metadata contract. |
| Optional upstream payload tracing | `MISSING` | No codex-lb-equivalent config-gated normalized payload dump path. |
| 4xx/5xx proxy error detail logging | `PARTIAL` | Several warning/error logs exist; full parity fields (`request_id`, method/path, normalized code/message everywhere) are incomplete. |

## High-Priority Parity Backlog (Ordered)

1. Implement real upstream transport (replace default stub transport path).
2. Implement `POST /v1/chat/completions` compatibility route and mapping.
3. Implement `GET /api/codex/usage` parity endpoint and payload model.
4. Wire and enforce API-key auth middleware and request-id/decompression middleware (currently stubs).
5. Wire HTTP session bridge into active `/v1/responses` and `/backend-api/codex/responses` paths.
6. Implement downstream/upstream turn-state continuity semantics for WS + HTTP responses.
7. Expand payload validation/sanitization parity (`include`, tool types, truncation, message-role rules, file_id rejection).
8. Implement streaming/compact retry-budget and error-code parity behavior.
9. Implement request-log persistence parity for proxy transport outcomes.
10. Add parity tests for all missing requirements above before feature completion claims.

