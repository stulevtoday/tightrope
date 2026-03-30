#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/contracts/reference_env.sh"

PID_FILE="${TMPDIR:-/tmp}/tightrope-reference-backend.pid"
LOG_FILE="${TMPDIR:-/tmp}/tightrope-reference-backend.log"

UV_RUNNER=()
USE_MODULE_FALLBACK=0

if command -v uv >/dev/null 2>&1; then
  UV_RUNNER=(uv)
elif [[ -x "$ROOT/.local-py/bin/uv" ]]; then
  UV_RUNNER=("$ROOT/.local-py/bin/uv")
elif [[ -d "$ROOT/.local-py/uv" ]]; then
  UV_RUNNER=(python3 -m uv)
  USE_MODULE_FALLBACK=1
else
  echo "No uv runner found. Install uv or provide $ROOT/.local-py fallback."
  exit 1
fi

if [[ -f "$PID_FILE" ]]; then
  EXISTING_PID="$(cat "$PID_FILE")"
  if kill -0 "$EXISTING_PID" 2>/dev/null; then
    echo "Reference backend already running (pid $EXISTING_PID)"
    exit 0
  fi
  rm -f "$PID_FILE"
fi

(
  cd "$REFERENCE_REPO_ROOT"
  if [[ "$USE_MODULE_FALLBACK" -eq 1 ]]; then
    nohup env "PYTHONPATH=$ROOT/.local-py${PYTHONPATH:+:$PYTHONPATH}" "${UV_RUNNER[@]}" run fastapi run app/main.py --host "$REFERENCE_BACKEND_HOST" --port "$REFERENCE_BACKEND_PORT" >"$LOG_FILE" 2>&1 &
  else
    nohup "${UV_RUNNER[@]}" run fastapi run app/main.py --host "$REFERENCE_BACKEND_HOST" --port "$REFERENCE_BACKEND_PORT" >"$LOG_FILE" 2>&1 &
  fi
  echo $! >"$PID_FILE"
)

for _ in $(seq 1 240); do
  if curl -fsS "$REFERENCE_BACKEND_URL/health" >/dev/null 2>&1; then
    echo "Reference backend is up at $REFERENCE_BACKEND_URL"
    exit 0
  fi
  sleep 0.25
done

echo "Reference backend failed to start; see $LOG_FILE"
exit 1
