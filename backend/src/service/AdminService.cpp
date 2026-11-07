#include "AdminService.hpp"

#include <regex>

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "security/AuthService.hpp"
#include "security/Crypto.hpp"

namespace cf {

using text::opt;
using text::trimmed;

static const char* USER_SELECT =
    "SELECT u.id, u.username, u.full_name, u.email, u.phone, u.is_active, u.last_login_at, u.created_at, "
    "  CASE WHEN u.locked_until > now() THEN u.locked_until END AS locked_until, d.id AS doctor_id, "
    "  coalesce((SELECT array_agg(r.code ORDER BY r.code) FROM user_roles ur JOIN roles r ON r.id = ur.role_id "
    "            WHERE ur.user_id = u.id), '{}') AS roles "
    "FROM users u LEFT JOIN doctors d ON d.user_id = u.id ";

static oatpp::Object<dto::UserDto> toUser(const Row& r) {
  auto u = dto::UserDto::createShared();
  u->id = r.s("id");
  u->username = r.s("username");
  u->fullName = r.s("full_name");
  u->email = r.s("email");
  u->phone = r.s("phone");
  u->isActive = r.b("is_active");
  u->roles = r.strArray("roles");
  u->doctorId = r.s("doctor_id");
  u->lastLoginAt = r.ts("last_login_at");
  u->lockedUntil = r.ts("locked_until");
  u->createdAt = r.ts("created_at");
  return u;
}

oatpp::Object<dto::UserDto> AdminService::getUser(Conn& c, const std::string& id) const {
  auto r = c.exec(std::string(USER_SELECT) + "WHERE u.id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("User");
  return toUser(r.first());
}

oatpp::Vector<oatpp::Object<dto::UserDto>> AdminService::listUsers() const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::UserDto>>::createShared();
    c.exec(std::string(USER_SELECT) + "ORDER BY u.full_name").each([&](const Row& r) { out->push_back(toUser(r)); });
    return out;
  });
}

void AdminService::setUserRoles(Conn& c, const std::string& userId, const oatpp::Vector<oatpp::String>& roles) const {
  if (!roles || roles->empty()) throw ApiError::badRequest("At least one role is required", {{"roles", "Select a role"}});
  c.exec("DELETE FROM user_roles WHERE user_id = $1", Params(userId));
  for (auto& code : *roles) {
    auto r = c.exec("INSERT INTO user_roles (user_id, role_id) SELECT $1, id FROM roles WHERE code = $2 RETURNING role_id",
                    Params(userId, code));
    if (r.empty()) throw ApiError::badRequest("Unknown role " + (code ? *code : std::string("")), {{"roles", "Unknown role"}});
  }
}

void AdminService::ensureAdminRemains(Conn& c) const {
  auto n = c.exec(
                "SELECT count(DISTINCT u.id) AS n FROM users u JOIN user_roles ur ON ur.user_id = u.id "
                "JOIN role_permissions rp ON rp.role_id = ur.role_id JOIN permissions p ON p.id = rp.permission_id "
                "WHERE u.is_active AND p.code = 'user.manage'")
               .first()
               .i64v("n");
  if (n == 0) throw ApiError::conflict("LAST_ADMIN", "At least one active user must retain user-management permission");
}

oatpp::Object<dto::UserDto> AdminService::createUser(const AuthContext& ctx, const oatpp::Object<dto::UserInputDto>& in) {
  Validator v;
  std::string username = trimmed(in->username);
  std::string fullName = trimmed(in->fullName);
  v.check(std::regex_match(username, std::regex("^[a-zA-Z0-9._-]{3,40}$")), "username",
          "3-40 characters: letters, numbers, dot, dash, underscore");
  v.check(fullName.size() >= 2 && fullName.size() <= 120, "fullName", "Full name is required");
  auto email = opt(in->email);
  if (email) v.check(text::isEmail(*email), "email", "Invalid email address");
  v.throwIfAny();
  AuthService::validatePasswordPolicy(in->password ? std::string(*in->password) : "", username);
  std::string hash = crypto::password::hash(*in->password);

  return m_db->tx([&](Conn& c) {
    try {
      auto id = c.exec("INSERT INTO users (username, full_name, email, phone, password_hash, must_change_password, is_active) "
                       "VALUES ($1, $2, $3, $4, $5, true, coalesce($6, true)) RETURNING id",
                       Params(username, fullName, email, opt(in->phone), hash, in->isActive))
                    .first()
                    .str("id");
      setUserRoles(c, id, in->roles);
      m_audit->record(c, ctx, AuditEvent{"USER_CREATE", "user", id, std::nullopt, "SUCCESS",
                                         {{"username", username}, {"roles", text::join(in->roles)}}});
      return getUser(c, id);
    } catch (const DbError& e) {
      if (e.isUniqueViolation()) throw ApiError::conflict("USERNAME_TAKEN", "Username already exists");
      throw;
    }
  });
}

oatpp::Object<dto::UserDto> AdminService::updateUser(const AuthContext& ctx, const std::string& id,
                                                     const oatpp::Object<dto::UserInputDto>& in) {
  Validator v;
  std::string fullName = trimmed(in->fullName);
  v.check(fullName.size() >= 2 && fullName.size() <= 120, "fullName", "Full name is required");
  auto email = opt(in->email);
  if (email) v.check(text::isEmail(*email), "email", "Invalid email address");
  v.throwIfAny();
  if (id == ctx.userId && in->isActive && !*in->isActive)
    throw ApiError::conflict("SELF_DEACTIVATION", "You cannot deactivate your own account");

  return m_db->tx([&](Conn& c) {
    auto before = getUser(c, id);
    c.exec("UPDATE users SET full_name = $2, email = $3, phone = $4, is_active = coalesce($5, is_active) WHERE id = $1",
           Params(id, fullName, email, opt(in->phone), in->isActive));
    if (in->roles) setUserRoles(c, id, in->roles);
    if (in->isActive && !*in->isActive) {
      c.exec("UPDATE user_sessions SET revoked_at = now(), revoke_reason = 'user_deactivated' "
             "WHERE user_id = $1 AND revoked_at IS NULL", Params(id));
    }
    ensureAdminRemains(c);
    auto after = getUser(c, id);
    m_audit->record(c, ctx, AuditEvent{"USER_UPDATE", "user", id, std::nullopt, "SUCCESS",
                                       {{"rolesBefore", text::join(before->roles)},
                                        {"rolesAfter", text::join(after->roles)},
                                        {"isActive", (bool)*after->isActive}}});
    return after;
  });
}

void AdminService::resetPassword(const AuthContext& ctx, const std::string& id, const std::string& newPassword) {
  auto username = m_db->withConn([&](Conn& c) { return std::string(*getUser(c, id)->username); });
  AuthService::validatePasswordPolicy(newPassword, username);
  std::string hash = crypto::password::hash(newPassword);
  m_db->tx([&](Conn& c) {
    c.exec("UPDATE users SET password_hash = $2, must_change_password = true, failed_login_count = 0, "
           "locked_until = NULL, password_changed_at = now() WHERE id = $1",
           Params(id, hash));
    c.exec("UPDATE user_sessions SET revoked_at = now(), revoke_reason = 'password_reset' "
           "WHERE user_id = $1 AND revoked_at IS NULL", Params(id));
    m_audit->record(c, ctx, AuditEvent{"USER_PASSWORD_RESET", "user", id, std::nullopt, "SUCCESS", {}});
  });
}

// ---------------- Roles ----------------
static const char* ROLE_SELECT =
    "SELECT r.id, r.code, r.name, r.description, r.is_system, "
    "  (SELECT count(*) FROM user_roles ur WHERE ur.role_id = r.id) AS user_count, "
    "  coalesce((SELECT array_agg(p.code ORDER BY p.code) FROM role_permissions rp "
    "            JOIN permissions p ON p.id = rp.permission_id WHERE rp.role_id = r.id), '{}') AS perms "
    "FROM roles r ";

static oatpp::Object<dto::RoleDto> toRole(const Row& r) {
  auto d = dto::RoleDto::createShared();
  d->id = r.s("id");
  d->code = r.s("code");
  d->name = r.s("name");
  d->description = r.s("description");
  d->isSystem = r.b("is_system");
  d->userCount = r.i32("user_count");
  d->permissions = r.strArray("perms");
  return d;
}

oatpp::Object<dto::RoleDto> AdminService::getRole(Conn& c, const std::string& id) const {
  auto r = c.exec(std::string(ROLE_SELECT) + "WHERE r.id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Role");
  return toRole(r.first());
}

oatpp::Vector<oatpp::Object<dto::RoleDto>> AdminService::listRoles() const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::RoleDto>>::createShared();
    c.exec(std::string(ROLE_SELECT) + "ORDER BY r.is_system DESC, r.name").each([&](const Row& r) { out->push_back(toRole(r)); });
    return out;
  });
}

oatpp::Vector<oatpp::Object<dto::PermissionDto>> AdminService::listPermissions() const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::PermissionDto>>::createShared();
    c.exec("SELECT code, module, description FROM permissions ORDER BY module, code").each([&](const Row& r) {
      auto p = dto::PermissionDto::createShared();
      p->code = r.s("code");
      p->module = r.s("module");
      p->description = r.s("description");
      out->push_back(p);
    });
    return out;
  });
}

static void setRolePermissions(Conn& c, const std::string& roleId, const oatpp::Vector<oatpp::String>& perms) {
  c.exec("DELETE FROM role_permissions WHERE role_id = $1", Params(roleId));
  if (!perms) return;
  for (auto& code : *perms) {
    auto r = c.exec("INSERT INTO role_permissions (role_id, permission_id) SELECT $1, id FROM permissions WHERE code = $2 "
                    "RETURNING permission_id", Params(roleId, code));
    if (r.empty()) throw ApiError::badRequest("Unknown permission " + (code ? *code : std::string("")));
  }
}

oatpp::Object<dto::RoleDto> AdminService::createRole(const AuthContext& ctx, const oatpp::Object<dto::RoleInputDto>& in) {
  Validator v;
  std::string code = trimmed(in->code);
  std::string name = trimmed(in->name);
  v.check(std::regex_match(code, std::regex("^[A-Z][A-Z0-9_]{1,39}$")), "code", "Uppercase letters, digits, underscore");
  v.check(name.size() >= 2, "name", "Name is required");
  v.throwIfAny();
  return m_db->tx([&](Conn& c) {
    try {
      auto id = c.exec("INSERT INTO roles (code, name, description) VALUES ($1, $2, $3) RETURNING id",
                       Params(code, name, opt(in->description))).first().str("id");
      setRolePermissions(c, id, in->permissions);
      m_audit->record(c, ctx, AuditEvent{"ROLE_CREATE", "role", id, std::nullopt, "SUCCESS",
                                         {{"code", code}, {"permissions", text::join(in->permissions)}}});
      return getRole(c, id);
    } catch (const DbError& e) {
      if (e.isUniqueViolation()) throw ApiError::conflict("ROLE_EXISTS", "Role code already exists");
      throw;
    }
  });
}

oatpp::Object<dto::RoleDto> AdminService::updateRole(const AuthContext& ctx, const std::string& id,
                                                     const oatpp::Object<dto::RoleInputDto>& in) {
  return m_db->tx([&](Conn& c) {
    auto before = getRole(c, id);
    std::string name = trimmed(in->name);
    c.exec("UPDATE roles SET name = coalesce(nullif($2, ''), name), description = coalesce($3, description) WHERE id = $1",
           Params(id, name, opt(in->description)));
    if (in->permissions) setRolePermissions(c, id, in->permissions);
    ensureAdminRemains(c);
    auto after = getRole(c, id);
    m_audit->record(c, ctx, AuditEvent{"ROLE_PERMISSIONS_UPDATE", "role", id, std::nullopt, "SUCCESS",
                                       {{"code", std::string(*after->code)},
                                        {"before", text::join(before->permissions)},
                                        {"after", text::join(after->permissions)}}});
    return after;
  });
}

// ---------------- Settings ----------------
oatpp::Object<dto::SettingsDto> AdminService::getSettings() const {
  return m_db->withConn([&](Conn& c) {
    auto r = c.exec("SELECT *, duplicate_rules::text AS rules FROM clinic_settings WHERE id = 1").first();
    auto s = dto::SettingsDto::createShared();
    s->clinicName = r.s("clinic_name");
    s->address = r.s("address");
    s->city = r.s("city");
    s->state = r.s("state");
    s->pincode = r.s("pincode");
    s->phone = r.s("phone");
    s->email = r.s("email");
    s->registrationNumber = r.s("registration_number");
    s->gstin = r.s("gstin");
    s->hfrId = r.s("hfr_id");
    s->timezone = r.s("timezone");
    s->patientIdPrefix = r.s("patient_id_prefix");
    s->invoicePrefix = r.s("invoice_prefix");
    s->defaultSlotMinutes = r.i32("default_slot_minutes");
    s->sessionIdleMinutes = r.i32("session_idle_minutes");
    s->updatedAt = r.ts("updated_at");
    auto j = nlohmann::json::parse(r.str("rules"));
    auto d = dto::DuplicateRulesDto::createShared();
    d->threshold = j.value("threshold", 60);
    d->nameSimilarityMin = j.value("nameSimilarityMin", 0.45);
    d->weightMobile = j["weights"].value("mobile", 40);
    d->weightDateOfBirth = j["weights"].value("dateOfBirth", 30);
    d->weightName = j["weights"].value("name", 30);
    d->weightGender = j["weights"].value("gender", 0);
    s->duplicateRules = d;
    return s;
  });
}

oatpp::Object<dto::SettingsDto> AdminService::updateSettings(const AuthContext& ctx,
                                                             const oatpp::Object<dto::SettingsDto>& in) {
  Validator v;
  std::string name = trimmed(in->clinicName);
  v.check(name.size() >= 2, "clinicName", "Clinic name is required");
  auto pin = opt(in->pincode);
  if (pin) v.check(std::regex_match(*pin, std::regex("^[1-9][0-9]{5}$")), "pincode", "6-digit PIN code");
  auto em = opt(in->email);
  if (em) v.check(text::isEmail(*em), "email", "Invalid email");
  if (in->patientIdPrefix) v.check(std::regex_match(std::string(*in->patientIdPrefix), std::regex("^[A-Z]{2,6}$")), "patientIdPrefix", "2-6 uppercase letters");
  if (in->invoicePrefix) v.check(std::regex_match(std::string(*in->invoicePrefix), std::regex("^[A-Z]{2,6}$")), "invoicePrefix", "2-6 uppercase letters");
  if (in->sessionIdleMinutes) v.check(*in->sessionIdleMinutes >= 5 && *in->sessionIdleMinutes <= 480, "sessionIdleMinutes", "5 to 480 minutes");
  if (in->defaultSlotMinutes) v.check(*in->defaultSlotMinutes >= 5 && *in->defaultSlotMinutes <= 120, "defaultSlotMinutes", "5 to 120 minutes");
  std::optional<std::string> rules;
  if (in->duplicateRules) {
    auto d = in->duplicateRules;
    int th = d->threshold ? *d->threshold : 60;
    double sim = d->nameSimilarityMin ? *d->nameSimilarityMin : 0.45;
    v.check(th >= 10 && th <= 100, "duplicateRules.threshold", "10 to 100");
    v.check(sim >= 0.1 && sim <= 1.0, "duplicateRules.nameSimilarityMin", "0.1 to 1.0");
    nlohmann::json j = {{"threshold", th}, {"nameSimilarityMin", sim},
                        {"weights", {{"mobile", d->weightMobile ? *d->weightMobile : 40},
                                     {"dateOfBirth", d->weightDateOfBirth ? *d->weightDateOfBirth : 30},
                                     {"name", d->weightName ? *d->weightName : 30},
                                     {"gender", d->weightGender ? *d->weightGender : 0}}}};
    for (auto& [k, w] : j["weights"].items()) v.check(w.get<int>() >= 0 && w.get<int>() <= 100, "duplicateRules." + k, "0 to 100");
    rules = j.dump();
  }
  v.throwIfAny();
  m_db->tx([&](Conn& c) {
    c.exec("UPDATE clinic_settings SET clinic_name = $1, address = $2, city = $3, state = $4, pincode = $5, phone = $6, "
           "email = $7, registration_number = $8, gstin = $9, hfr_id = $10, "
           "patient_id_prefix = coalesce($11, patient_id_prefix), invoice_prefix = coalesce($12, invoice_prefix), "
           "default_slot_minutes = coalesce($13, default_slot_minutes), "
           "session_idle_minutes = coalesce($14, session_idle_minutes), "
           "duplicate_rules = coalesce($15::jsonb, duplicate_rules), updated_at = now(), updated_by = $16 WHERE id = 1",
           Params(name, opt(in->address), opt(in->city), opt(in->state), pin, opt(in->phone), em,
                  opt(in->registrationNumber), opt(in->gstin), opt(in->hfrId), opt(in->patientIdPrefix),
                  opt(in->invoicePrefix), in->defaultSlotMinutes, in->sessionIdleMinutes, rules, ctx.userId));
    m_audit->record(c, ctx, AuditEvent{"SETTINGS_UPDATE", "clinic_settings", "1", std::nullopt, "SUCCESS",
                                       {{"duplicateRulesChanged", rules.has_value()}}});
  });
  return getSettings();
}

// ---------------- Dashboard ----------------
oatpp::Object<dto::DashboardDto> AdminService::dashboard(const AuthContext& ctx) const {
  return m_db->withConn([&](Conn& c) {
    auto d = dto::DashboardDto::createShared();
    // "Today" is evaluated in the clinic's timezone.
    auto r = c.exec(
        "WITH tz AS (SELECT timezone FROM clinic_settings WHERE id = 1), "
        "day AS (SELECT (now() AT TIME ZONE (SELECT timezone FROM tz))::date AS d), "
        "b AS (SELECT ((SELECT d FROM day)::timestamp AT TIME ZONE (SELECT timezone FROM tz)) AS s) "
        "SELECT (SELECT d FROM day)::text AS today, "
        " (SELECT count(*) FROM patients WHERE is_active) AS total_patients, "
        " (SELECT count(*) FROM patients WHERE created_at >= (SELECT s FROM b)) AS new_patients, "
        " (SELECT count(*) FROM appointments WHERE start_at >= (SELECT s FROM b) AND start_at < (SELECT s FROM b) + interval '1 day' "
        "    AND status NOT IN ('CANCELLED')) AS appts_today, "
        " (SELECT count(*) FROM queue_tokens WHERE queue_date = (SELECT d FROM day) AND status IN ('WAITING','CALLED')) AS waiting, "
        " (SELECT count(*) FROM queue_tokens WHERE queue_date = (SELECT d FROM day) AND status = 'IN_CONSULTATION') AS in_consult, "
        " (SELECT count(*) FROM consultations WHERE status = 'FINALIZED' AND finalized_at >= (SELECT s FROM b)) AS done_today, "
        " (SELECT count(*) FROM bills WHERE status IN ('PENDING','PARTIALLY_PAID')) AS pending_count, "
        " (SELECT coalesce(sum(total_paise - paid_paise), 0) FROM bills WHERE status IN ('PENDING','PARTIALLY_PAID')) AS pending_paise, "
        " (SELECT coalesce(sum(CASE WHEN payment_type = 'PAYMENT' THEN amount_paise ELSE -amount_paise END), 0) "
        "    FROM payments WHERE received_at >= (SELECT s FROM b)) AS revenue_today");
    auto row = r.first();
    d->date = row.s("today");
    d->totalPatients = row.i64("total_patients");
    d->newPatientsToday = row.i64("new_patients");
    d->appointmentsToday = row.i64("appts_today");
    d->waitingNow = row.i64("waiting");
    d->inConsultationNow = row.i64("in_consult");
    d->consultationsCompletedToday = row.i64("done_today");
    if (ctx.has(perm::BILLING_READ)) {
      d->pendingPaymentsCount = row.i64("pending_count");
      d->pendingPaymentsPaise = row.i64("pending_paise");
    }
    if (ctx.has(perm::REPORT_BILLING)) {
      d->revenueTodayPaise = row.i64("revenue_today");
      d->revenueLast7DaysPaise = oatpp::Vector<oatpp::Int64>::createShared();
      d->last7Days = oatpp::Vector<oatpp::String>::createShared();
      c.exec("WITH tz AS (SELECT timezone FROM clinic_settings WHERE id = 1), "
             "days AS (SELECT generate_series((now() AT TIME ZONE (SELECT timezone FROM tz))::date - 6, "
             "                                (now() AT TIME ZONE (SELECT timezone FROM tz))::date, interval '1 day')::date AS d) "
             "SELECT days.d::text AS day, coalesce(sum(CASE WHEN p.payment_type = 'PAYMENT' THEN p.amount_paise ELSE -p.amount_paise END), 0) AS amt "
             "FROM days LEFT JOIN payments p ON (p.received_at AT TIME ZONE (SELECT timezone FROM tz))::date = days.d "
             "GROUP BY days.d ORDER BY days.d")
          .each([&](const Row& x) {
            d->last7Days->push_back(x.s("day"));
            d->revenueLast7DaysPaise->push_back(x.i64("amt"));
          });
    }
    d->appointmentsByStatus = oatpp::Fields<oatpp::Int64>::createShared();
    c.exec("WITH tz AS (SELECT timezone FROM clinic_settings WHERE id = 1), "
           "b AS (SELECT (((now() AT TIME ZONE (SELECT timezone FROM tz))::date)::timestamp AT TIME ZONE (SELECT timezone FROM tz)) AS s) "
           "SELECT status, count(*) AS n FROM appointments WHERE start_at >= (SELECT s FROM b) "
           "AND start_at < (SELECT s FROM b) + interval '1 day' GROUP BY status")
        .each([&](const Row& x) { d->appointmentsByStatus->push_back({x.s("status"), x.i64("n")}); });
    return d;
  });
}

}  // namespace cf
