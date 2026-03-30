#include "conflict_resolver.h"

#include <array>
#include <vector>

namespace tightrope::sync {

namespace {

struct TableRule {
    std::string_view table;
    ConflictStrategy strategy;
    bool replicated;
};

constexpr std::array<TableRule, 11> kTableRules = {{
    {.table = "accounts", .strategy = ConflictStrategy::RaftLinearizable, .replicated = true},
    {.table = "dashboard_settings", .strategy = ConflictStrategy::RaftLinearizable, .replicated = true},
    {.table = "api_keys", .strategy = ConflictStrategy::RaftLinearizable, .replicated = true},
    {.table = "api_key_limits", .strategy = ConflictStrategy::RaftLinearizable, .replicated = true},
    {.table = "ip_allowlist", .strategy = ConflictStrategy::CrdtOrSetAddWins, .replicated = true},
    {.table = "usage_history", .strategy = ConflictStrategy::CrdtPnCounterMerge, .replicated = true},
    {.table = "sticky_sessions", .strategy = ConflictStrategy::LwwByHlc, .replicated = true},
    {.table = "request_logs", .strategy = ConflictStrategy::LocalOnly, .replicated = false},
    {.table = "_sync_journal", .strategy = ConflictStrategy::LocalOnly, .replicated = false},
    {.table = "_sync_meta", .strategy = ConflictStrategy::LocalOnly, .replicated = false},
    {.table = "_sync_last_seen", .strategy = ConflictStrategy::LocalOnly, .replicated = false},
}};

const TableRule* find_rule(const std::string_view table_name) {
    for (const auto& rule : kTableRules) {
        if (rule.table == table_name) {
            return &rule;
        }
    }

    if (table_name.rfind("_sync_", 0) == 0 || table_name.rfind("_raft_", 0) == 0) {
        static constexpr TableRule local_prefix_rule{
            .table = "",
            .strategy = ConflictStrategy::LocalOnly,
            .replicated = false,
        };
        return &local_prefix_rule;
    }
    return nullptr;
}

} // namespace

ConflictDecision resolve_lww(const Hlc& remote, const Hlc& local) {
    return compare_hlc(remote, local) > 0 ? ConflictDecision::ApplyRemote : ConflictDecision::KeepLocal;
}

ConflictStrategy conflict_strategy_for_table(const std::string_view table_name) {
    if (const auto* rule = find_rule(table_name); rule != nullptr) {
        return rule->strategy;
    }
    return ConflictStrategy::LwwByHlc;
}

bool is_table_replicated(const std::string_view table_name) {
    if (const auto* rule = find_rule(table_name); rule != nullptr) {
        return rule->replicated;
    }
    return true;
}

std::vector<std::string_view> replicated_table_names() {
    std::vector<std::string_view> names;
    names.reserve(kTableRules.size());
    for (const auto& rule : kTableRules) {
        if (rule.replicated) {
            names.push_back(rule.table);
        }
    }
    return names;
}

bool table_requires_raft(const std::string_view table_name) {
    return conflict_strategy_for_table(table_name) == ConflictStrategy::RaftLinearizable;
}

ConflictDecision resolve_table_conflict(const std::string_view table_name, const Hlc& remote, const Hlc& local) {
    switch (conflict_strategy_for_table(table_name)) {
    case ConflictStrategy::LocalOnly:
    case ConflictStrategy::RaftLinearizable:
        return ConflictDecision::KeepLocal;
    case ConflictStrategy::LwwByHlc:
    case ConflictStrategy::CrdtPnCounterMerge:
    case ConflictStrategy::CrdtOrSetAddWins:
        return resolve_lww(remote, local);
    }
    return ConflictDecision::KeepLocal;
}

} // namespace tightrope::sync
