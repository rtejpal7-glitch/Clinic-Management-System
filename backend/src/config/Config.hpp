#pragma once
// Runtime configuration, read once from environment variables at startup.
// Secrets (DB passwords, encryption keys) are only ever read from the
// environment / secret store — never hardcoded or committed.

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace cf {

struct Config {
  std::string appEnv;                // development | production | test
  std::string host;
  int port = 8000;
  std::string databaseUrl;           // least-privilege runtime role
  std::string migrationDatabaseUrl;  // schema owner role (migrations only)
  int dbPoolSize = 8;
  std::string migrationsDir;
  std::string dataEncryptionKeyB64;  // AES-256-GCM key for field-level encryption
  std::string blindIndexKeyB64;      // HMAC-SHA256 key for searchable blind indexes
  std::string bootstrapAdminUsername;
  std::string bootstrapAdminPassword;
  std::string abdmMode;              // mock (only mode implemented in the PoC)
  int sessionAbsoluteHours = 12;
  bool trustProxyHeaders = false;
  std::string swaggerResPath;
  bool runMigrations = true;
  int workerIntervalSeconds = 30;

  static std::string env(const char* name, const std::string& def = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : def;
  }
  static std::string required(const char* name) {
    auto v = env(name);
    if (v.empty()) throw std::runtime_error(std::string("Missing required environment variable: ") + name);
    return v;
  }

  static Config fromEnv() {
    Config c;
    c.appEnv = env("APP_ENV", "development");
    c.host = env("HOST", "0.0.0.0");
    c.port = std::stoi(env("PORT", "8000"));
    c.databaseUrl = required("DATABASE_URL");
    c.migrationDatabaseUrl = env("MIGRATION_DATABASE_URL", c.databaseUrl);
    c.dbPoolSize = std::stoi(env("DB_POOL_SIZE", "8"));
    c.migrationsDir = env("MIGRATIONS_DIR", "./migrations");
    c.dataEncryptionKeyB64 = required("DATA_ENCRYPTION_KEY");
    c.blindIndexKeyB64 = required("BLIND_INDEX_KEY");
    c.bootstrapAdminUsername = env("BOOTSTRAP_ADMIN_USERNAME", "admin");
    c.bootstrapAdminPassword = env("BOOTSTRAP_ADMIN_PASSWORD", "");
    c.abdmMode = env("ABDM_MODE", "mock");
    c.sessionAbsoluteHours = std::stoi(env("SESSION_ABSOLUTE_HOURS", "12"));
    c.trustProxyHeaders = env("TRUST_PROXY_HEADERS", "false") == "true";
    c.swaggerResPath = env("SWAGGER_RES_PATH", "");
    c.runMigrations = env("RUN_MIGRATIONS", "true") == "true";
    c.workerIntervalSeconds = std::stoi(env("WORKER_INTERVAL_SECONDS", "30"));
    if (c.abdmMode != "mock") {
      throw std::runtime_error("ABDM_MODE='" + c.abdmMode +
                               "' is not implemented. Only 'mock' is available in this PoC; a live adapter "
                               "requires ABDM onboarding, sandbox credentials and implementation of AbdmGateway.");
    }
    return c;
  }
};

}  // namespace cf
