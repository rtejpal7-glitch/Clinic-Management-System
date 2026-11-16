#include "RoleSetup.hpp"

#include <cctype>
#include <stdexcept>

#include "Db.hpp"
#include "oatpp/core/base/Environment.hpp"

namespace cf {

ConnUrlParts splitPostgresUrl(const std::string& url) {
  ConnUrlParts p;
  for (const char* s : {"postgresql://", "postgres://"}) {
    if (url.rfind(s, 0) == 0) { p.scheme = s; break; }
  }
  if (p.scheme.empty())
    throw std::runtime_error("Database URL must start with postgresql:// (key=value connection strings are not supported here)");
  std::string after = url.substr(p.scheme.size());
  auto slash = after.find('/');
  std::string authority = slash == std::string::npos ? after : after.substr(0, slash);
  auto at = authority.rfind('@');
  if (at == std::string::npos) {
    p.rest = after;
  } else {
    p.userinfo = after.substr(0, at);
    p.rest = after.substr(at + 1);
  }
  if (p.rest.empty()) throw std::runtime_error("Database URL has no host");
  return p;
}

std::string urlEncodeCredential(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

std::string withCredentials(const std::string& url, const std::string& user, const std::string& password) {
  auto p = splitPostgresUrl(url);
  return p.scheme + urlEncodeCredential(user) + ":" + urlEncodeCredential(password) + "@" + p.rest;
}

namespace roles {

void ensureAppRole(const std::string& adminUrl, const std::string& password) {
  if (password.size() < 16) throw std::runtime_error("APP_DB_PASSWORD must be at least 16 characters");
  auto c = Db::connect(adminUrl);
  c->exec("SELECT pg_advisory_lock(727002)");
  try {
    bool exists = !c->exec("SELECT 1 FROM pg_roles WHERE rolname = $1", Params(std::string(kAppRole))).empty();
    // New roles default to NOSUPERUSER, NOBYPASSRLS, NOCREATEDB, NOCREATEROLE; checkRuntimeRole verifies.
    // format() quotes the name and password server-side; no SQL is built from raw input.
    std::string stmt = c->exec(exists ? "SELECT format('ALTER ROLE %I WITH LOGIN PASSWORD %L', $1::text, $2::text) AS s"
                                      : "SELECT format('CREATE ROLE %I WITH LOGIN PASSWORD %L', $1::text, $2::text) AS s",
                               Params(std::string(kAppRole), password))
                           .first()
                           .str("s");
    try {
      c->exec(stmt);
    } catch (const DbError& e) {
      throw std::runtime_error(
          std::string("Could not ") + (exists ? "update" : "create") + " the database role clinicflow_app with the login in "
          "MIGRATION_DATABASE_URL (" + e.what() + "). That login needs the CREATEROLE privilege. Either create the role once "
          "yourself (see docs/DEPLOY_RENDER.md, 'If the role cannot be created'), or set ALLOW_SHARED_DB_ROLE=true to run "
          "with a single database login.");
    }
    // Lets the runtime role connect; table privileges come from the migrations.
    std::string db = c->exec("SELECT current_database() AS d").first().str("d");
    c->exec(c->exec("SELECT format('GRANT CONNECT ON DATABASE %I TO %I', $1::text, $2::text) AS s", Params(db, std::string(kAppRole)))
                .first()
                .str("s"));
    OATPP_LOGI("ClinicFlow", "Database role clinicflow_app %s", exists ? "updated" : "created");
  } catch (...) {
    c->exec("SELECT pg_advisory_unlock(727002)");
    throw;
  }
  c->exec("SELECT pg_advisory_unlock(727002)");
}

void checkRuntimeRole(const std::string& appUrl, bool allowSharedRole) {
  auto c = Db::connect(appUrl);
  auto r = c->exec(
                "SELECT current_user AS u, r.rolsuper AS su, r.rolbypassrls AS br, "
                "  EXISTS (SELECT 1 FROM pg_tables WHERE schemaname = 'public' AND tableowner = current_user) AS owns "
                "FROM pg_roles r WHERE r.rolname = current_user")
               .first();
  std::string who = r.str("u");
  if (r.boolv("su") || r.boolv("br"))
    throw std::runtime_error("The API's database login '" + who + "' is a superuser or has BYPASSRLS, which would switch off "
                             "clinic isolation. Use a separate least-privilege login (APP_DB_PASSWORD or DATABASE_URL).");
  if (r.boolv("owns")) {
    if (!allowSharedRole)
      throw std::runtime_error("The API's database login '" + who + "' owns the schema. Use the least-privilege role "
                               "(set APP_DB_PASSWORD), or set ALLOW_SHARED_DB_ROLE=true to accept a single login for a demo.");
    OATPP_LOGW("ClinicFlow", "ALLOW_SHARED_DB_ROLE: the API runs as the schema owner '%s'. Row-level security still applies "
               "(FORCE), but the least-privilege grants do not. Use for demos only.", who.c_str());
  }
}

}  // namespace roles
}  // namespace cf
