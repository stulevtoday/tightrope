#pragma once
// State-based CRDT merge protocol

#include <string>

#include <sqlite3.h>

#include "crdt_store.h"

namespace tightrope::sync::crdt {

class CrdtSync {
public:
    static bool merge_usage_counter(
        sqlite3* db,
        const std::string& key,
        const PNCounter& remote,
        PNCounter* merged = nullptr
    );

    static bool merge_sticky_session(
        sqlite3* db,
        const std::string& key,
        const LWWRegister<std::string>& remote,
        LWWRegister<std::string>* merged = nullptr
    );

    static bool merge_ip_allowlist(sqlite3* db, const std::string& key, const ORSet& remote, ORSet* merged = nullptr);
};

} // namespace tightrope::sync::crdt
