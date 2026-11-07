#pragma once
// Minimal versioned migration runner.
// Files: migrations/V<NNN>__<name>.sql, applied in order, each in its own
// transaction, recorded in schema_migrations with a SHA-256 checksum. A changed
// checksum for an already-applied migration aborts startup (migrations are immutable).

#include <string>

namespace cf {

class Migrator {
 public:
  static void run(const std::string& databaseUrl, const std::string& dir);
};

}  // namespace cf
