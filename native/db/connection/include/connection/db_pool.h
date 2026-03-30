#pragma once

#include <sqlite3.h>

// Connection pool interface

namespace tightrope::db {

class DbPool {
  public:
    virtual ~DbPool() = default;
    [[nodiscard]] virtual sqlite3* connection() const noexcept = 0;
};

} // namespace tightrope::db
