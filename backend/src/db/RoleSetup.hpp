#pragma once
// Hosted PostgreSQL (Render, Neon, Supabase…) gives one admin-like login, not a
// superuser, so db/init/00-roles.sh cannot run there. When APP_DB_PASSWORD is set
// and DATABASE_URL is not, the server uses the hosted login (MIGRATION_DATABASE_URL)
// to create or update the least-privilege runtime role clinicflow_app before
// migrations, and connects with that role. See docs/DEPLOY_RENDER.md.

#include <string>

namespace cf {

struct ConnUrlParts {
  std::string scheme;    // "postgresql://" or "postgres://"
  std::string userinfo;  // before '@' (may be empty)
  std::string rest;      // host[:port]/db[?params]
};

// Splits a postgres URL. Throws std::runtime_error for key=value conninfo strings.
ConnUrlParts splitPostgresUrl(const std::string& url);
// Percent-encodes a user name or password for use inside a URL.
std::string urlEncodeCredential(const std::string& s);
// Same host, database and options; different login.
std::string withCredentials(const std::string& url, const std::string& user, const std::string& password);

namespace roles {
constexpr const char* kAppRole = "clinicflow_app";
// CREATE ROLE clinicflow_app LOGIN … (or ALTER its password) using the admin URL.
// Throws with a clear message if the hosted login may not create roles.
void ensureAppRole(const std::string& adminUrl, const std::string& password);
// Refuses to run the API as a role that would ignore clinic isolation (superuser or
// BYPASSRLS). With allowSharedRole=false it also refuses to run as the schema owner.
void checkRuntimeRole(const std::string& appUrl, bool allowSharedRole);
}  // namespace roles

}  // namespace cf
