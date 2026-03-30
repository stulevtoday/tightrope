#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REFERENCE_ROOT="${REFERENCE_REPO_ROOT:-/Users/fabian/Development/codex-lb}"

"$ROOT/build-debug/tightrope-streaming-contract-capture" \
  --reference-root "$REFERENCE_ROOT" \
  --fixture-root "$ROOT/native/tests/contracts/fixtures/streaming"
