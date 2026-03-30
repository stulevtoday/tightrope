#!/usr/bin/env bash
set -euo pipefail

PID_FILE="${TMPDIR:-/tmp}/tightrope-reference-backend.pid"

if [[ ! -f "$PID_FILE" ]]; then
  echo "Reference backend is not running"
  exit 0
fi

PID="$(cat "$PID_FILE")"
if kill -0 "$PID" 2>/dev/null; then
  kill "$PID"
  wait "$PID" 2>/dev/null || true
  echo "Stopped reference backend (pid $PID)"
else
  echo "Reference backend pid file was stale ($PID)"
fi

rm -f "$PID_FILE"
