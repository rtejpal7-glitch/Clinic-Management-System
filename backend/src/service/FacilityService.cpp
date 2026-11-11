#include "FacilityService.hpp"

#include <regex>

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "security/AuthService.hpp"
#include "security/Crypto.hpp"

namespace cf {

using text::opt;
using text::trimmed;

static const char* FACILITY_SELECT =
    "SELECT f.id, f.code, f.name, f.specialty_code, f.city, f.state, f.hfr_id, f.is_active, f.network_sharing_enabled, "
    "  f.network_agreement_ref, f.network_enabled_at, f.created_at, "
    "  (SELECT count(*) FROM users u WHERE u.facility_id = f.id AND u.is_active) AS user_count "
    "FROM facilities f ";

static oatpp::Object<dto::FacilityDto> toFacility(const Row& r) {
  auto f = dto::FacilityDto::createShared();
  f->id = r.s("id");
  f->code = r.s("code");
  f->name = r.s("name");
  f->specialty = r.s("specialty_code");
  f->city = r.s("city");
  f->state = r.s("state");
  f->hfrId = r.s("hfr_id");
  f->isActive = r.b("is_active");
  f->networkSharingEnabled = r.b("network_sharing_enabled");
  f->networkAgreementRef = r.s("network_agreement_ref");
  f->networkEnabledAt = r.ts("network_enabled_at");
  f->userCount = r.i64("user_count");
  f->createdAt = r.ts("created_at");
  return f;
}

oatpp::Object<dto::FacilityDto> FacilityService::get(Conn& c, const std::string& id) const {
  auto r = c.exec(std::string(FACILITY_SELECT) + "WHERE f.id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Clinic");
  return toFacility(r.first());
}

oatpp::Vector<oatpp::Object<dto::FacilityDto>> FacilityService::list(const AuthContext& ctx) const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::FacilityDto>>::createShared();
    bool all = ctx.has(perm::PLATFORM_MANAGE);
    c.exec(std::string(FACILITY_SELECT) + "WHERE $1 OR f.id = $2::uuid ORDER BY f.created_at", Params(all, ctx.facilityId))
        .each([&](const Row& r) { out->push_back(toFacility(r)); });
    return out;
  });
}

oatpp::Object<dto::FacilityDto> FacilityService::current(const AuthContext& ctx) const {
  return m_db->withConn([&](Conn& c) { return get(c, ctx.facilityId); });
}

oatpp::Object<dto::FacilityDto> FacilityService::create(const AuthContext& ctx,
                                                        const oatpp::Object<dto::FacilityCreateDto>& in) {
  Validator v;
  std::string code = trimmed(in->code);
  std::string name = trimmed(in->name);
  std::string specialty = in->specialty ? trimmed(in->specialty) : "DERMATOLOGY";
  std::string adminUser = trimmed(in->adminUsername);
  std::string adminName = trimmed(in->adminFullName);
  v.check(std::regex_match(code, std::regex("^[A-Z0-9]{2,12}$")), "code", "2-12 uppercase letters or digits");
  v.check(name.size() >= 2 && name.size() <= 120, "name", "Clinic name is required");
  v.check(specialty == "DERMATOLOGY" || specialty == "GENERAL", "specialty", "DERMATOLOGY or GENERAL");
  auto pin = opt(in->pincode);
  if (pin) v.check(std::regex_match(*pin, std::regex("^[1-9][0-9]{5}$")), "pincode", "6-digit PIN code");
  v.check(std::regex_match(adminUser, std::regex("^[a-zA-Z0-9._-]{3,40}$")), "adminUsername",
          "3-40 characters: letters, numbers, dot, dash, underscore");
  v.check(adminName.size() >= 2, "adminFullName", "Administrator name is required");
  v.throwIfAny();
  std::string pw = in->adminPassword ? std::string(*in->adminPassword) : "";
  AuthService::validatePasswordPolicy(pw, adminUser);
  std::string hash = crypto::password::hash(pw);

  // Seeding a new clinic writes rows into a facility other than the caller's, so this
  // (platform-only) operation runs with an explicit, scoped RLS bypass and sets
  // facility_id on every row it writes.
  ScopedFacility scope(ctx.facilityId, true);
  return m_db->tx([&](Conn& c) {
    std::string id;
    try {
      id = c.exec("INSERT INTO facilities (code, name, specialty_code, city, state, pincode, phone) "
                  "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
                  Params(code, name, specialty, opt(in->city), opt(in->state), pin, opt(in->phone)))
               .first()
               .str("id");
    } catch (const DbError& e) {
      if (e.isUniqueViolation()) throw ApiError::conflict("CLINIC_CODE_TAKEN", "Clinic code already exists");
      throw;
    }
    // Starter service list copied from the primary clinic; prices are then the new clinic's own.
    c.exec("INSERT INTO service_catalogue (code, name, category, price_paise, tax_rate_bp, is_active, facility_id) "
           "SELECT code, name, category, price_paise, tax_rate_bp, is_active, $1 FROM service_catalogue "
           "WHERE facility_id = $2::uuid",
           Params(id, DEFAULT_FACILITY_ID));
    std::string userId;
    try {
      userId = c.exec("INSERT INTO users (username, full_name, password_hash, must_change_password, facility_id) "
                      "VALUES ($1, $2, $3, true, $4) RETURNING id",
                      Params(adminUser, adminName, hash, id))
                   .first()
                   .str("id");
    } catch (const DbError& e) {
      if (e.isUniqueViolation())
        throw ApiError(409, "USERNAME_TAKEN", "Username already exists", {{"adminUsername", "Already taken"}});
      throw;
    }
    c.exec("INSERT INTO user_roles (user_id, role_id) SELECT $1, id FROM roles WHERE code = 'ADMIN' AND facility_id IS NULL",
           Params(userId));
    m_audit->record(c, ctx, AuditEvent{"FACILITY_CREATE", "facility", id, std::nullopt, "SUCCESS",
                                       {{"code", code}, {"specialty", specialty}, {"firstAdmin", adminUser}}});
    return get(c, id);
  });
}

oatpp::Object<dto::FacilityDto> FacilityService::setNetworkSharing(const AuthContext& ctx,
                                                                   const oatpp::Object<dto::NetworkSharingDto>& in) {
  bool enable = in->enabled && *in->enabled;
  auto ref = opt(in->agreementRef);
  if (enable && (!ref || ref->size() < 3 || ref->size() > 120))
    throw ApiError::badRequest("A data-sharing agreement reference is required to join the network",
                               {{"agreementRef", "Enter the agreement / governance approval reference"}});
  return m_db->tx([&](Conn& c) {
    if (enable) {
      c.exec("UPDATE facilities SET network_sharing_enabled = true, network_agreement_ref = $2, "
             "network_enabled_at = now(), network_enabled_by = $3 WHERE id = $1",
             Params(ctx.facilityId, *ref, ctx.userId));
    } else {
      // Leaving the network stops new contributions; already de-identified records are
      // withdrawn from aggregation (network_case_records.withdrawn_at).
      c.exec("UPDATE facilities SET network_sharing_enabled = false WHERE id = $1", Params(ctx.facilityId));
      if (m_onLeave) m_onLeave(c, ctx.facilityId);
    }
    m_audit->record(c, ctx, AuditEvent{enable ? "NETWORK_SHARING_ENABLE" : "NETWORK_SHARING_DISABLE", "facility",
                                       ctx.facilityId, std::nullopt, "SUCCESS",
                                       {{"agreementRef", ref.value_or("")}}});
    return get(c, ctx.facilityId);
  });
}

}  // namespace cf
