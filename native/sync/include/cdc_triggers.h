#pragma once
// Trigger SQL generation per table

#include <string>
#include <vector>

namespace tightrope::sync {

struct CdcTableSpec {
    std::string table_name;
    std::vector<std::string> primary_key_columns;
    std::vector<std::string> tracked_columns;
};

struct CdcTriggerSql {
    std::string insert_trigger;
    std::string update_trigger;
    std::string delete_trigger;
};

CdcTriggerSql build_cdc_triggers(const CdcTableSpec& spec);

} // namespace tightrope::sync
