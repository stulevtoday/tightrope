#include "cdc_triggers.h"

#include <sstream>
#include <stdexcept>

namespace tightrope::sync {

namespace {

std::string json_object_for_columns(const std::string_view alias, const std::vector<std::string>& columns) {
    std::ostringstream sql;
    sql << "json_object(";
    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (index > 0) {
            sql << ", ";
        }
        sql << "'" << columns[index] << "', " << alias << "." << columns[index];
    }
    sql << ")";
    return sql.str();
}

std::string where_clause_for_pk(const std::string_view alias, const std::vector<std::string>& columns) {
    std::ostringstream sql;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (index > 0) {
            sql << " AND ";
        }
        sql << columns[index] << " = " << alias << "." << columns[index];
    }
    return sql.str();
}

} // namespace

CdcTriggerSql build_cdc_triggers(const CdcTableSpec& spec) {
    if (spec.table_name.empty()) {
        throw std::runtime_error("table_name is required");
    }
    if (spec.primary_key_columns.empty()) {
        throw std::runtime_error("at least one primary key column is required");
    }

    const auto row_pk_new = json_object_for_columns("NEW", spec.primary_key_columns);
    const auto row_pk_old = json_object_for_columns("OLD", spec.primary_key_columns);
    const auto new_values = json_object_for_columns("NEW", spec.tracked_columns);
    const auto old_values = json_object_for_columns("OLD", spec.tracked_columns);
    const auto update_where = where_clause_for_pk("NEW", spec.primary_key_columns);

    CdcTriggerSql sql{};

    sql.insert_trigger =
        "CREATE TRIGGER _cdc_" + spec.table_name + "_insert AFTER INSERT ON " + spec.table_name + "\n"
        "BEGIN\n"
        "  INSERT INTO _sync_journal (hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum)\n"
        "  VALUES (\n"
        "    _hlc_now_wall(), _hlc_now_counter(), _hlc_site_id(),\n"
        "    '" +
        spec.table_name + "', " + row_pk_new + ", 'INSERT',\n"
                                        "    NULL,\n"
                                        "    " +
        new_values + ",\n"
                     "    _checksum('" +
        spec.table_name + "', " + row_pk_new + ", 'INSERT', NULL, " + new_values + ")\n"
                                                                         "  );\n"
                                                                         "  UPDATE " +
        spec.table_name +
        " SET _hlc_wall = _hlc_now_wall(), _hlc_counter = _hlc_now_counter(), _hlc_site = _hlc_site_id()\n"
        "    WHERE " +
        update_where + ";\n"
                       "END;";

    sql.update_trigger =
        "CREATE TRIGGER _cdc_" + spec.table_name + "_update AFTER UPDATE ON " + spec.table_name + "\n"
        "WHEN OLD._hlc_wall != _hlc_now_wall() OR OLD._hlc_counter != _hlc_now_counter()\n"
        "BEGIN\n"
        "  INSERT INTO _sync_journal (hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum)\n"
        "  VALUES (\n"
        "    _hlc_now_wall(), _hlc_now_counter(), _hlc_site_id(),\n"
        "    '" +
        spec.table_name + "', " + row_pk_new + ", 'UPDATE',\n"
                                        "    " +
        old_values + ",\n"
                     "    " +
        new_values + ",\n"
                     "    _checksum('" +
        spec.table_name + "', " + row_pk_new + ", 'UPDATE', " + old_values + ", " + new_values + ")\n"
                                                                                            "  );\n"
                                                                                            "  UPDATE " +
        spec.table_name +
        " SET _hlc_wall = _hlc_now_wall(), _hlc_counter = _hlc_now_counter(), _hlc_site = _hlc_site_id()\n"
        "    WHERE " +
        update_where + ";\n"
                       "END;";

    sql.delete_trigger =
        "CREATE TRIGGER _cdc_" + spec.table_name + "_delete AFTER DELETE ON " + spec.table_name + "\n"
        "BEGIN\n"
        "  INSERT INTO _sync_journal (hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum)\n"
        "  VALUES (\n"
        "    _hlc_now_wall(), _hlc_now_counter(), _hlc_site_id(),\n"
        "    '" +
        spec.table_name + "', " + row_pk_old + ", 'DELETE',\n"
                                        "    " +
        old_values + ",\n"
                     "    NULL,\n"
                     "    _checksum('" +
        spec.table_name + "', " + row_pk_old + ", 'DELETE', " + old_values + ", NULL)\n"
                                                                                            "  );\n"
                                                                                            "  INSERT OR REPLACE INTO _sync_tombstones (table_name, row_pk, deleted_at, site_id)\n"
                                                                                            "    VALUES ('" +
        spec.table_name + "', " + row_pk_old + ", _hlc_now_wall(), _hlc_site_id());\n"
                                  "END;";

    return sql;
}

} // namespace tightrope::sync
