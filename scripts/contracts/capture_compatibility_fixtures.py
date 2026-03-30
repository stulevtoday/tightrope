#!/usr/bin/env python3
"""
Generate tightrope compatibility fixtures without external service execution.

Produces:
- native/tests/contracts/fixtures/db/legacy_store.db
- native/tests/contracts/fixtures/crypto/encryption.key
- native/tests/contracts/fixtures/crypto/token_blob.txt
"""

from __future__ import annotations

import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DB_PATH = ROOT / "native" / "tests" / "contracts" / "fixtures" / "db" / "legacy_store.db"
KEY_PATH = ROOT / "native" / "tests" / "contracts" / "fixtures" / "crypto" / "encryption.key"
BLOB_PATH = ROOT / "native" / "tests" / "contracts" / "fixtures" / "crypto" / "token_blob.txt"


def write_db() -> None:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("PRAGMA user_version = 1;")
        conn.execute("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY);")
        conn.execute("INSERT OR REPLACE INTO schema_version(version) VALUES (1);")
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS accounts (
                id TEXT PRIMARY KEY,
                status TEXT NOT NULL,
                plan TEXT NOT NULL,
                reset_at TEXT
            );
            """
        )
        conn.execute(
            """
            INSERT OR REPLACE INTO accounts(id, status, plan, reset_at)
            VALUES ('acct_legacy_1', 'active', 'pro', '2026-03-28T00:00:00Z');
            """
        )


def write_crypto() -> None:
    KEY_PATH.parent.mkdir(parents=True, exist_ok=True)
    KEY_PATH.write_text("tightrope-compat-key-v1:6QpU7QvS2m8pJ2Y2Wl8F0A==\n", encoding="utf-8")
    BLOB_PATH.write_text(
        "v1.tightrope.compat.blob:eyJhbGciOiJkaXIiLCJlbmMiOiJBMjU2R0NNIn0..legacy-sample-token\n",
        encoding="utf-8",
    )


def main() -> None:
    write_db()
    write_crypto()
    print(f"Wrote {DB_PATH}")
    print(f"Wrote {KEY_PATH}")
    print(f"Wrote {BLOB_PATH}")


if __name__ == "__main__":
    main()
