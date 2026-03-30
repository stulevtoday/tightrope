#include "crdt/crdt_sync.h"

namespace tightrope::sync::crdt {

bool CrdtSync::merge_usage_counter(
    sqlite3* db,
    const std::string& key,
    const PNCounter& remote,
    PNCounter* merged
) {
    PNCounter local;
    if (!CrdtStore::load_pn_counter(db, key, local)) {
        return false;
    }

    local.merge(remote);
    if (!CrdtStore::save_pn_counter(db, key, local)) {
        return false;
    }
    if (merged != nullptr) {
        *merged = local;
    }
    return true;
}

bool CrdtSync::merge_sticky_session(
    sqlite3* db,
    const std::string& key,
    const LWWRegister<std::string>& remote,
    LWWRegister<std::string>* merged
) {
    LWWRegister<std::string> local;
    if (!CrdtStore::load_lww_string(db, key, local)) {
        return false;
    }

    local.merge(remote);
    if (!CrdtStore::save_lww_string(db, key, local)) {
        return false;
    }
    if (merged != nullptr) {
        *merged = local;
    }
    return true;
}

bool CrdtSync::merge_ip_allowlist(sqlite3* db, const std::string& key, const ORSet& remote, ORSet* merged) {
    ORSet local;
    if (!CrdtStore::load_or_set(db, key, local)) {
        return false;
    }

    local.merge(remote);
    if (!CrdtStore::save_or_set(db, key, local)) {
        return false;
    }
    if (merged != nullptr) {
        *merged = local;
    }
    return true;
}

} // namespace tightrope::sync::crdt
