#include "AuthService.hpp"

#include <cctype>

#include "common/ApiError.hpp"
#include "oatpp/core/base/Environment.hpp"
#include "security/Crypto.hpp"

namespace cf {

namespace {
// Pre-computed Argon2id hash of a random string; verifying against it for unknown
// usernames keeps response time similar to a real failed login.
const std::string& dummyHash() {
  static const std::string h = crypto::password::hash(crypto::randomToken(24));
  return h;
}
}  // namespace

void AuthService::validatePasswordPolicy(const std::string& pw, const std::string& username) {
  Validator v;
  bool hasLetter = false, hasDigit = false;
  for (unsigned char ch : pw) {
    if (std::isalpha(ch)) hasLetter = true;
    if (std::isdigit(ch)) hasDigit = true;
  }
  v.check(pw.size() >= 10, "password", "Password must be at least 10 characters");
  v.check(pw.size() <= 128, "password", "Password must be at most 128 characters");
  v.check(hasLetter && hasDigit, "password", "Password must contain letters and numbers");
  std::string lowerPw = pw, lowerUser = username;
  for (auto& c : lowerPw) c = std::tolower(static_cast<unsigned char>(c));
  for (auto& c : lowerUser) c = std::tolower(static_cast<unsigned char>(c));
  v.check(lowerUser.empty() || lowerPw.find(lowerUser) == std::string::npos, "password",
          "Password must not contain the username");
  v.throwIfAny("Password does not meet the policy");
}

oatpp::Object<dto::LoginResponseDto> AuthService::login(const std::string& username, const std::string& password,
                                                        const RequestMeta& meta) {
  if (username.empty() || password.empty() || username.size() > 64 || password.size() > 256) {
    throw ApiError::badRequest("Username and password are required");
  }
  auto invalid = [&](const std::string& reason, std::optional<std::string> userId) {
    AuditActor actor = AuditActor::anonymous(username, meta);
    actor.userId = userId;
    m_audit->recordStandalone(actor, AuditEvent{"AUTH_LOGIN", "user", userId.value_or(""), std::nullopt, "FAILURE",
                                                {{"reason", reason}}});
    return ApiError(401, "INVALID_CREDENTIALS", "Invalid username or password");
  };

  auto rows = m_db->withConn([&](Conn& c) {
    return c.exec("SELECT id, password_hash, is_active, failed_login_count, "
                  "(locked_until IS NOT NULL AND locked_until > now()) AS locked "
                  "FROM users WHERE lower(username) = lower($1)",
                  Params(username));
  });
  if (rows.empty()) {
    crypto::password::verify(dummyHash(), password);
    throw invalid("unknown_user", std::nullopt);
  }
  auto u = rows.first();
  std::string userId = u.str("id");
  if (u.boolv("locked")) {
    crypto::password::verify(dummyHash(), password);
    m_audit->recordStandalone(AuditActor{userId, username, "", meta},
                              AuditEvent{"AUTH_LOGIN", "user", userId, std::nullopt, "DENIED", {{"reason", "account_locked"}}});
    throw ApiError(423, "ACCOUNT_LOCKED", "Account temporarily locked after repeated failed attempts. Try again later.");
  }
  bool ok = crypto::password::verify(u.str("password_hash"), password);
  if (!ok) {
    m_db->withConn([&](Conn& c) {
      c.exec("UPDATE users SET failed_login_count = failed_login_count + 1, "
             "locked_until = CASE WHEN failed_login_count + 1 >= $2 THEN now() + make_interval(mins => $3) "
             "ELSE locked_until END WHERE id = $1",
             Params(userId, MAX_FAILED_ATTEMPTS, LOCKOUT_MINUTES));
    });
    throw invalid("bad_password", userId);
  }
  if (!u.boolv("is_active")) throw invalid("inactive_user", userId);

  std::string token = crypto::randomToken(32);
  std::string tokenHash = crypto::sha256Hex(token);
  std::string expiresAt = m_db->tx([&](Conn& c) {
    c.exec("UPDATE users SET failed_login_count = 0, locked_until = NULL, last_login_at = now() WHERE id = $1",
           Params(userId));
    auto s = c.exec("INSERT INTO user_sessions (user_id, token_hash, expires_at, ip_address, user_agent) "
                    "VALUES ($1, $2, now() + make_interval(hours => $3), $4, $5) RETURNING id, expires_at",
                    Params(userId, tokenHash, m_absoluteHours, meta.ip, meta.userAgent.substr(0, 300)));
    m_audit->record(c, AuditActor{userId, username, "", meta},
                    AuditEvent{"AUTH_LOGIN", "user", userId, std::nullopt, "SUCCESS",
                               {{"sessionId", s.first().str("id")}}});
    return *s.first().ts("expires_at");
  });

  AuthContext ctx = authenticate(oatpp::String("Bearer " + token), meta, perm::AUTHENTICATED);
  auto resp = dto::LoginResponseDto::createShared();
  resp->token = token;
  resp->expiresAt = expiresAt;
  resp->user = currentUser(ctx);
  return resp;
}

void AuthService::logout(const AuthContext& ctx) {
  m_db->tx([&](Conn& c) {
    c.exec("UPDATE user_sessions SET revoked_at = now(), revoke_reason = 'logout' WHERE id = $1",
           Params(ctx.sessionId));
    m_audit->record(c, ctx, AuditEvent{"AUTH_LOGOUT", "user", ctx.userId, std::nullopt, "SUCCESS", {}});
  });
}

AuthContext AuthService::authenticate(const oatpp::String& header, const RequestMeta& meta,
                                      const std::string& requiredPermission) {
  std::string h = header ? std::string(*header) : "";
  const std::string prefix = "Bearer ";
  if (h.size() <= prefix.size() || h.compare(0, prefix.size(), prefix) != 0) throw ApiError::unauthorized();
  std::string token = h.substr(prefix.size());
  if (token.size() < 20 || token.size() > 100) throw ApiError::unauthorized();
  std::string tokenHash = crypto::sha256Hex(token);

  AuthContext ctx;
  ctx.meta = meta;
  // Resolve identity before any facility is known: sessions/users are not tenant
  // tables, but the doctor profile join is, so this lookup runs with a scoped bypass.
  DbContext::clear();
  ScopedFacility resolving("", true);
  bool found = m_db->withConn([&](Conn& c) {
    auto r = c.exec(
        "SELECT s.id AS sid, u.id AS uid, u.username, u.full_name, u.must_change_password, "
        "       (SELECT session_idle_minutes FROM clinic_settings WHERE id = 1) AS idle, "
        "       (s.last_seen_at < now() - interval '60 seconds') AS stale, d.id AS doctor_id, "
        "       f.id AS facility_id, f.code AS facility_code, f.name AS facility_name, f.specialty_code "
        "FROM user_sessions s JOIN users u ON u.id = s.user_id "
        "JOIN facilities f ON f.id = u.facility_id AND f.is_active "
        "LEFT JOIN doctors d ON d.user_id = u.id AND d.is_active AND d.facility_id = u.facility_id "
        "WHERE s.token_hash = $1 AND s.revoked_at IS NULL AND s.expires_at > now() AND u.is_active "
        "  AND s.last_seen_at > now() - make_interval(mins => (SELECT session_idle_minutes FROM clinic_settings WHERE id = 1))",
        Params(tokenHash));
    if (r.empty()) return false;
    auto row = r.first();
    ctx.sessionId = row.str("sid");
    ctx.userId = row.str("uid");
    ctx.username = row.str("username");
    ctx.fullName = row.str("full_name");
    ctx.mustChangePassword = row.boolv("must_change_password");
    ctx.sessionIdleMinutes = static_cast<int>(row.i64v("idle", 30));
    if (!row.isNull("doctor_id")) ctx.doctorId = row.str("doctor_id");
    ctx.facilityId = row.str("facility_id");
    ctx.facilityCode = row.str("facility_code");
    ctx.facilityName = row.str("facility_name");
    ctx.specialty = row.str("specialty_code");
    // Sliding idle window, throttled to one write per minute.
    if (row.boolv("stale")) c.exec("UPDATE user_sessions SET last_seen_at = now() WHERE id = $1", Params(ctx.sessionId));
    auto perms = c.exec(
        "SELECT r.code AS role, p.code AS perm FROM user_roles ur JOIN roles r ON r.id = ur.role_id "
        "LEFT JOIN role_permissions rp ON rp.role_id = r.id LEFT JOIN permissions p ON p.id = rp.permission_id "
        "WHERE ur.user_id = $1",
        Params(ctx.userId));
    perms.each([&](const Row& pr) {
      auto role = pr.str("role");
      if (std::find(ctx.roles.begin(), ctx.roles.end(), role) == ctx.roles.end()) ctx.roles.push_back(role);
      if (!pr.isNull("perm")) ctx.permissions.insert(pr.str("perm"));
    });
    return true;
  });
  if (!found) throw ApiError(401, "SESSION_INVALID", "Session expired or invalid. Please sign in again.");
  // From here on, every connection this request checks out is pinned to the caller's
  // facility; the outer guard() clears it when the request ends.
  resolving.commit(ctx.facilityId);
  // Accounts with a temporary password (new or reset) may only change their password.
  if (ctx.mustChangePassword && !requiredPermission.empty())
    throw ApiError(403, "PASSWORD_CHANGE_REQUIRED", "You must change your temporary password before continuing");
  require(ctx, requiredPermission);
  return ctx;
}

void AuthService::require(const AuthContext& ctx, const std::string& permission, const std::string& resource) const {
  if (permission.empty() || ctx.has(permission)) return;
  m_audit->recordStandalone(AuditActor::from(ctx),
                            AuditEvent{"ACCESS_DENIED", "permission", permission, std::nullopt, "DENIED",
                                       {{"requiredPermission", permission}, {"resource", resource}}});
  throw ApiError::forbidden();
}

oatpp::Object<dto::CurrentUserDto> AuthService::currentUser(const AuthContext& ctx) const {
  auto u = dto::CurrentUserDto::createShared();
  u->id = ctx.userId;
  u->username = ctx.username;
  u->fullName = ctx.fullName;
  u->roles = oatpp::Vector<oatpp::String>::createShared();
  for (auto& r : ctx.roles) u->roles->push_back(r);
  u->permissions = oatpp::Vector<oatpp::String>::createShared();
  for (auto& p : ctx.permissions) u->permissions->push_back(p);
  u->doctorId = ctx.doctorId ? oatpp::String(*ctx.doctorId) : nullptr;
  u->mustChangePassword = ctx.mustChangePassword;
  u->sessionIdleMinutes = ctx.sessionIdleMinutes;
  u->facilityId = ctx.facilityId;
  u->facilityCode = ctx.facilityCode;
  u->facilityName = ctx.facilityName;
  u->specialty = ctx.specialty;
  return u;
}

void AuthService::changePassword(const AuthContext& ctx, const std::string& current, const std::string& next) {
  auto hash = m_db->withConn([&](Conn& c) {
    return c.exec("SELECT password_hash FROM users WHERE id = $1", Params(ctx.userId)).first().str("password_hash");
  });
  if (!crypto::password::verify(hash, current)) {
    m_audit->recordStandalone(AuditActor::from(ctx), AuditEvent{"AUTH_PASSWORD_CHANGE", "user", ctx.userId,
                                                                std::nullopt, "FAILURE", {{"reason", "bad_current"}}});
    throw ApiError::badRequest("Current password is incorrect", {{"currentPassword", "Incorrect password"}});
  }
  if (current == next) throw ApiError::badRequest("New password must differ", {{"newPassword", "Must be different"}});
  validatePasswordPolicy(next, ctx.username);
  std::string newHash = crypto::password::hash(next);
  m_db->tx([&](Conn& c) {
    c.exec("UPDATE users SET password_hash = $2, must_change_password = false, password_changed_at = now() "
           "WHERE id = $1",
           Params(ctx.userId, newHash));
    // Revoke all other sessions of this user
    c.exec("UPDATE user_sessions SET revoked_at = now(), revoke_reason = 'password_changed' "
           "WHERE user_id = $1 AND id <> $2 AND revoked_at IS NULL",
           Params(ctx.userId, ctx.sessionId));
    m_audit->record(c, ctx, AuditEvent{"AUTH_PASSWORD_CHANGE", "user", ctx.userId, std::nullopt, "SUCCESS", {}});
  });
}

void AuthService::bootstrapAdmin(const Config& cfg) {
  bool hasUsers = m_db->withConn(
      [&](Conn& c) { return c.exec("SELECT EXISTS (SELECT 1 FROM users) AS e").first().boolv("e"); });
  if (hasUsers) return;
  if (cfg.bootstrapAdminPassword.empty()) {
    throw std::runtime_error("No users exist. Set BOOTSTRAP_ADMIN_PASSWORD to create the initial administrator.");
  }
  validatePasswordPolicy(cfg.bootstrapAdminPassword, cfg.bootstrapAdminUsername);
  std::string hash = crypto::password::hash(cfg.bootstrapAdminPassword);
  m_db->tx([&](Conn& c) {
    auto id = c.exec("INSERT INTO users (username, full_name, password_hash, must_change_password, facility_id) "
                     "VALUES ($1, 'Clinic Administrator', $2, $3, '00000000-0000-4000-8000-000000000001') RETURNING id",
                     Params(cfg.bootstrapAdminUsername, hash, cfg.appEnv == "production"))
                  .first()
                  .str("id");
    c.exec("INSERT INTO user_roles (user_id, role_id) SELECT $1, id FROM roles "
           "WHERE code IN ('ADMIN','PLATFORM_ADMIN') AND facility_id IS NULL", Params(id));
    m_audit->record(c, AuditActor{std::nullopt, "system", "", {"127.0.0.1", "bootstrap"}},
                    AuditEvent{"USER_CREATE", "user", id, std::nullopt, "SUCCESS", {{"bootstrap", true}}});
  });
  OATPP_LOGI("Auth", "Created initial administrator '%s'", cfg.bootstrapAdminUsername.c_str());
}

}  // namespace cf
