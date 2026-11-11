#include "Migrator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#include "Db.hpp"
#include "oatpp/core/base/Environment.hpp"
#include "security/Crypto.hpp"

namespace cf {

void Migrator::run(const std::string& databaseUrl, const std::string& dir) {
  namespace fs = std::filesystem;
  if (!fs::exists(dir)) throw std::runtime_error("Migrations directory not found: " + dir);

  std::vector<fs::path> files;
  std::regex pattern(R"(^V(\d{3})__[a-z0-9_]+\.sql$)");
  for (auto& e : fs::directory_iterator(dir)) {
    if (e.is_regular_file() && std::regex_match(e.path().filename().string(), pattern)) files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());

  auto conn = Db::connect(databaseUrl);
  conn->exec("SELECT pg_advisory_lock(727001)");  // serialize concurrent starters
  // Migrations run as the schema owner; FORCE ROW LEVEL SECURITY applies to the owner
  // too, so data migrations must explicitly see every facility's rows.
  conn->exec("SELECT set_config('app.rls_bypass', 'on', false)");
  try {
    conn->exec(
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        " version text PRIMARY KEY, name text NOT NULL, checksum text NOT NULL,"
        " applied_at timestamptz NOT NULL DEFAULT now())");

    for (auto& f : files) {
      std::string filename = f.filename().string();
      std::string version = filename.substr(1, 3);
      std::ifstream in(f);
      std::stringstream ss;
      ss << in.rdbuf();
      std::string sql = ss.str();
      std::string checksum = crypto::sha256Hex(sql);

      auto existing = conn->exec("SELECT checksum FROM schema_migrations WHERE version = $1", Params(version));
      if (!existing.empty()) {
        if (existing.first().str("checksum") != checksum) {
          throw std::runtime_error("Checksum mismatch for applied migration " + filename +
                                   ". Applied migrations must never be edited; add a new migration instead.");
        }
        continue;
      }
      OATPP_LOGI("Migrator", "Applying %s", filename.c_str());
      conn->execRaw("BEGIN");
      try {
        conn->execRaw(sql);
        conn->exec("INSERT INTO schema_migrations(version, name, checksum) VALUES ($1, $2, $3)",
                   Params(version, filename, checksum));
        conn->execRaw("COMMIT");
      } catch (...) {
        conn->execRaw("ROLLBACK");
        throw;
      }
    }
  } catch (...) {
    conn->exec("SELECT pg_advisory_unlock(727001)");
    throw;
  }
  conn->exec("SELECT pg_advisory_unlock(727001)");
  OATPP_LOGI("Migrator", "Schema is up to date (%d migration files)", (int)files.size());
}

}  // namespace cf
