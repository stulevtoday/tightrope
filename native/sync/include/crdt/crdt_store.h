#pragma once
// Serialization and SQLite persistence for CRDT state

#include <string>

#include <sqlite3.h>

#include "lww_register.h"
#include "or_set.h"
#include "pn_counter.h"

namespace tightrope::sync::crdt {

class CrdtStore {
public:
    static bool ensure_schema(sqlite3* db);

    static bool save_pn_counter(sqlite3* db, const std::string& key, const PNCounter& counter);
    static bool load_pn_counter(sqlite3* db, const std::string& key, PNCounter& out);

    static bool save_lww_string(sqlite3* db, const std::string& key, const LWWRegister<std::string>& reg);
    static bool load_lww_string(sqlite3* db, const std::string& key, LWWRegister<std::string>& out);

    static bool save_or_set(sqlite3* db, const std::string& key, const ORSet& set);
    static bool load_or_set(sqlite3* db, const std::string& key, ORSet& out);
};

} // namespace tightrope::sync::crdt
