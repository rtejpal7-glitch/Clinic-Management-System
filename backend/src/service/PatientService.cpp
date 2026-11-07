#include "PatientService.hpp"

#include <algorithm>
#include <regex>

#include "common/ApiError.hpp"
#include "common/Text.hpp"

namespace cf {

using text::opt;
using text::trimmed;

const char* PATIENT_SELECT =
    "SELECT p.*, p.date_of_birth::text AS dob, date_part('year', age(p.date_of_birth))::int AS age_years, "
    "  EXISTS (SELECT 1 FROM patient_identifiers i WHERE i.patient_id = p.id AND i.verification_status = 'VERIFIED') "
    "    AS abha_linked "
    "FROM patients p ";

oatpp::Object<dto::PatientDto> patientFromRow(const Row& r) {
  auto p = dto::PatientDto::createShared();
  p->id = r.s("id");
  p->patientNumber = r.s("patient_number");
  p->fullName = r.s("full_name");
  p->dateOfBirth = r.s("dob");
  p->dobIsEstimated = r.b("dob_is_estimated");
  p->ageYears = r.i32("age_years");
  p->gender = r.s("gender");
  p->mobile = r.s("mobile");
  p->email = r.s("email");
  p->addressLine = r.s("address_line");
  p->city = r.s("city");
  p->district = r.s("district");
  p->state = r.s("state");
  p->pincode = r.s("pincode");
  p->emergencyContactName = r.s("emergency_contact_name");
  p->emergencyContactRelation = r.s("emergency_contact_relation");
  p->emergencyContactPhone = r.s("emergency_contact_phone");
  p->bloodGroup = r.s("blood_group");
  p->maritalStatus = r.s("marital_status");
  p->occupation = r.s("occupation");
  p->preferredLanguage = r.s("preferred_language");
  p->isActive = r.b("is_active");
  p->abhaLinked = r.b("abha_linked");
  p->version = r.i32("version");
  p->createdAt = r.ts("created_at");
  p->updatedAt = r.ts("updated_at");
  return p;
}

struct PatientService::Fields {
  std::string fullName, dob, gender, mobile;
  bool dobEstimated = false;
  std::optional<std::string> email, addressLine, city, district, state, pincode, ecName, ecRelation, ecPhone,
      bloodGroup, maritalStatus, occupation, language;

  // Ordered field map used for change-history diffs.
  std::vector<std::pair<std::string, std::optional<std::string>>> asList() const {
    return {{"full_name", fullName}, {"date_of_birth", dob}, {"dob_is_estimated", dobEstimated ? "true" : "false"},
            {"gender", gender}, {"mobile", mobile}, {"email", email}, {"address_line", addressLine},
            {"city", city}, {"district", district}, {"state", state}, {"pincode", pincode},
            {"emergency_contact_name", ecName}, {"emergency_contact_relation", ecRelation},
            {"emergency_contact_phone", ecPhone}, {"blood_group", bloodGroup}, {"marital_status", maritalStatus},
            {"occupation", occupation}, {"preferred_language", language}};
  }
};

static std::string collapseSpaces(const std::string& s) {
  std::string out;
  bool sp = false;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      sp = true;
      continue;
    }
    if (sp && !out.empty()) out += ' ';
    sp = false;
    out += c;
  }
  return out;
}

PatientService::Fields PatientService::validate(const oatpp::Object<dto::PatientInputDto>& in) const {
  Validator v;
  Fields f;
  f.fullName = collapseSpaces(trimmed(in->fullName));
  f.dob = trimmed(in->dateOfBirth);
  f.gender = text::upper(trimmed(in->gender));
  f.mobile = trimmed(in->mobile);
  f.dobEstimated = in->dobIsEstimated && *in->dobIsEstimated;
  f.email = opt(in->email);
  f.addressLine = opt(in->addressLine);
  f.city = opt(in->city);
  f.district = opt(in->district);
  f.state = opt(in->state);
  f.pincode = opt(in->pincode);
  f.ecName = opt(in->emergencyContactName);
  f.ecRelation = opt(in->emergencyContactRelation);
  f.ecPhone = opt(in->emergencyContactPhone);
  f.bloodGroup = opt(in->bloodGroup);
  f.maritalStatus = opt(in->maritalStatus);
  if (f.maritalStatus) f.maritalStatus = text::upper(*f.maritalStatus);
  f.occupation = opt(in->occupation);
  f.language = opt(in->preferredLanguage);

  v.check(f.fullName.size() >= 2 && f.fullName.size() <= 120, "fullName", "Full name must be 2-120 characters");
  v.check(f.fullName.find_first_of("0123456789<>{}") == std::string::npos, "fullName", "Name contains invalid characters");
  v.check(text::isIsoDate(f.dob), "dateOfBirth", "Date of birth is required (YYYY-MM-DD)");
  v.check(text::oneOf(f.gender, {"MALE", "FEMALE", "OTHER", "UNDISCLOSED"}), "gender", "Select a gender");
  v.check(text::isIndianMobile(f.mobile), "mobile", "Enter a valid 10-digit Indian mobile number");
  if (f.email) v.check(text::isEmail(*f.email), "email", "Invalid email address");
  if (f.pincode) v.check(std::regex_match(*f.pincode, std::regex("^[1-9][0-9]{5}$")), "pincode", "Enter a 6-digit PIN code");
  if (f.ecPhone) v.check(text::isIndianMobile(*f.ecPhone), "emergencyContactPhone", "Enter a valid 10-digit mobile number");
  if (f.ecPhone && *f.ecPhone == f.mobile) v.fail("emergencyContactPhone", "Emergency contact should differ from patient mobile");
  if (f.bloodGroup)
    v.check(text::oneOf(*f.bloodGroup, {"A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-"}), "bloodGroup", "Invalid blood group");
  if (f.maritalStatus)
    v.check(text::oneOf(*f.maritalStatus, {"SINGLE", "MARRIED", "DIVORCED", "WIDOWED", "OTHER"}), "maritalStatus", "Invalid value");
  for (auto* s : {&f.addressLine, &f.city, &f.district, &f.state, &f.ecName, &f.ecRelation, &f.occupation, &f.language})
    if (*s && (*s)->size() > 200) v.fail("address", "Value too long");
  v.throwIfAny();

  // Date sanity checked in SQL-independent way: not in the future, not > 130 years.
  bool dateOk = m_db->withConn([&](Conn& c) {
    return c.exec("SELECT ($1::date <= current_date AND $1::date > current_date - interval '130 years') AS ok", Params(f.dob))
        .first()
        .boolv("ok");
  });
  if (!dateOk) throw ApiError::badRequest("Invalid date of birth", {{"dateOfBirth", "Date must be in the past 130 years"}});
  return f;
}

DuplicateRules PatientService::loadRules(Conn& c) const {
  auto j = nlohmann::json::parse(c.exec("SELECT duplicate_rules::text AS r FROM clinic_settings WHERE id = 1").first().str("r"));
  DuplicateRules r;
  r.threshold = j.value("threshold", 60);
  r.nameSimilarityMin = j.value("nameSimilarityMin", 0.45);
  auto w = j.value("weights", nlohmann::json::object());
  r.weightMobile = w.value("mobile", 40);
  r.weightDateOfBirth = w.value("dateOfBirth", 30);
  r.weightName = w.value("name", 30);
  r.weightGender = w.value("gender", 0);
  return r;
}

std::vector<std::pair<oatpp::Object<dto::PatientDto>, DuplicateScore>> PatientService::findDuplicates(
    Conn& c, const Fields& f, const std::string& excludeId) const {
  auto rules = loadRules(c);
  std::string sql = std::string(PATIENT_SELECT);
  sql.replace(sql.find("FROM patients p "), 16,
              ", s.sim FROM patients p CROSS JOIN LATERAL (SELECT similarity(lower(p.full_name), lower($1)) AS sim) s ");
  auto rows = c.exec(sql +
                         "WHERE p.is_active AND ($4::uuid IS NULL OR p.id <> $4::uuid) "
                         "  AND (p.mobile = $3 OR p.date_of_birth = $2::date OR s.sim >= $5::float8) "
                         "ORDER BY s.sim DESC LIMIT 50",
                     Params(f.fullName, f.dob, f.mobile, excludeId.empty() ? std::optional<std::string>() : excludeId,
                            std::to_string(rules.nameSimilarityMin)));
  std::vector<std::pair<oatpp::Object<dto::PatientDto>, DuplicateScore>> out;
  DuplicateProbe probe{text::lower(f.fullName), f.dob, f.mobile, f.gender};
  rows.each([&](const Row& r) {
    double sim = r.f64("sim").getValue(0.0);
    DuplicateCandidateFacts facts{r.str("dob"), r.str("mobile"), r.str("gender"), sim};
    auto score = scoreDuplicate(rules, probe, facts);
    if (score.isPossibleDuplicate) out.emplace_back(patientFromRow(r), score);
  });
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.second.score > b.second.score; });
  if (out.size() > 10) out.resize(10);
  return out;
}

oatpp::Object<dto::DuplicateCheckResultDto> PatientService::duplicateCheck(const oatpp::Object<dto::PatientInputDto>& in) const {
  auto f = validate(in);
  return m_db->withConn([&](Conn& c) {
    auto res = dto::DuplicateCheckResultDto::createShared();
    res->threshold = loadRules(c).threshold;
    res->candidates = oatpp::Vector<oatpp::Object<dto::DuplicateCandidateDto>>::createShared();
    for (auto& [p, s] : findDuplicates(c, f, "")) {
      auto cand = dto::DuplicateCandidateDto::createShared();
      cand->patient = p;
      cand->score = s.score;
      cand->matchedOn = oatpp::Vector<oatpp::String>::createShared();
      for (auto& m : s.matchedOn) cand->matchedOn->push_back(m);
      res->candidates->push_back(cand);
    }
    res->possibleDuplicates = !res->candidates->empty();
    return res;
  });
}

void PatientService::requireExists(Conn& c, const std::string& patientId) const {
  if (!text::isUuid(patientId)) throw ApiError::notFound("Patient");
  if (c.exec("SELECT 1 FROM patients WHERE id = $1", Params(patientId)).empty()) throw ApiError::notFound("Patient");
}

oatpp::Object<dto::PatientDto> PatientService::create(const AuthContext& ctx, const oatpp::Object<dto::PatientInputDto>& in) {
  auto f = validate(in);
  bool confirmed = in->confirmNotDuplicate && *in->confirmNotDuplicate;
  auto overrideReason = opt(in->duplicateOverrideReason);

  return m_db->tx([&](Conn& c) {
    // Serialize registrations so two receptionists cannot create the same
    // patient concurrently between the duplicate check and the insert.
    c.exec("SELECT pg_advisory_xact_lock(727003)");
    auto dups = findDuplicates(c, f, "");
    nlohmann::json dupIds = nlohmann::json::array();
    for (auto& [p, s] : dups) dupIds.push_back({{"id", std::string(*p->id)}, {"score", s.score}});
    if (!dups.empty()) {
      if (!confirmed) {
        nlohmann::json cands = nlohmann::json::array();
        for (auto& [p, s] : dups)
          cands.push_back({{"id", std::string(*p->id)}, {"patientNumber", std::string(*p->patientNumber)},
                           {"fullName", std::string(*p->fullName)}, {"dateOfBirth", std::string(*p->dateOfBirth)},
                           {"gender", std::string(*p->gender)}, {"mobile", std::string(*p->mobile)},
                           {"score", s.score}, {"matchedOn", s.matchedOn}});
        throw ApiError::withData(409, "POSSIBLE_DUPLICATE",
                                 "Possible existing patient record(s) found. Review before registering.",
                                 {{"candidates", cands}});
      }
      if (!overrideReason || overrideReason->size() < 5) {
        throw ApiError::badRequest("A reason is required to register despite possible duplicates",
                                   {{"duplicateOverrideReason", "Explain why this is a different person"}});
      }
    }
    auto settings = c.exec("SELECT patient_id_prefix, (now() AT TIME ZONE timezone)::date AS d, "
                           "extract(year FROM now() AT TIME ZONE timezone)::int AS y FROM clinic_settings WHERE id = 1")
                        .first();
    std::string year = settings.str("y");
    long long seq = c.exec("INSERT INTO id_counters (counter_name, period, last_value) VALUES ('patient', $1, 1) "
                           "ON CONFLICT (counter_name, period) DO UPDATE SET last_value = id_counters.last_value + 1 "
                           "RETURNING last_value",
                           Params(year))
                        .first()
                        .i64v("last_value");
    char num[16];
    snprintf(num, sizeof(num), "%06lld", seq);
    std::string patientNumber = settings.str("patient_id_prefix") + "-" + year + "-" + num;

    auto id = c.exec(
                   "INSERT INTO patients (patient_number, full_name, date_of_birth, dob_is_estimated, gender, mobile, email, "
                   "address_line, city, district, state, pincode, emergency_contact_name, emergency_contact_relation, "
                   "emergency_contact_phone, blood_group, marital_status, occupation, preferred_language, created_by, updated_by) "
                   "VALUES ($1,$2,$3::date,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$20) RETURNING id",
                   Params(patientNumber, f.fullName, f.dob, f.dobEstimated, f.gender, f.mobile, f.email, f.addressLine,
                          f.city, f.district, f.state, f.pincode, f.ecName, f.ecRelation, f.ecPhone, f.bloodGroup,
                          f.maritalStatus, f.occupation, f.language, ctx.userId))
                  .first()
                  .str("id");
    nlohmann::json details = {{"patientNumber", patientNumber}};
    if (!dups.empty()) {
      details["duplicateOverride"] = true;
      details["overrideReason"] = *overrideReason;
      details["candidates"] = dupIds;
      m_audit->record(c, ctx, AuditEvent{"PATIENT_DUPLICATE_OVERRIDE", "patient", id, id, "SUCCESS", details});
    }
    m_audit->record(c, ctx, AuditEvent{"PATIENT_CREATE", "patient", id, id, "SUCCESS", {{"patientNumber", patientNumber}}});
    return patientFromRow(c.exec(std::string(PATIENT_SELECT) + "WHERE p.id = $1", Params(id)).first());
  });
}

oatpp::Object<dto::PatientDto> PatientService::get(const AuthContext& ctx, const std::string& id, bool auditView) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Patient");
  return m_db->tx([&](Conn& c) {
    auto r = c.exec(std::string(PATIENT_SELECT) + "WHERE p.id = $1", Params(id));
    if (r.empty()) throw ApiError::notFound("Patient");
    if (auditView) m_audit->record(c, ctx, AuditEvent{"PATIENT_VIEW", "patient", id, id, "SUCCESS", {}});
    return patientFromRow(r.first());
  });
}

oatpp::Object<dto::PatientDto> PatientService::update(const AuthContext& ctx, const std::string& id,
                                                      const oatpp::Object<dto::PatientInputDto>& in) {
  if (!text::isUuid(id)) throw ApiError::notFound("Patient");
  if (!in->version) throw ApiError::badRequest("version is required for updates", {{"version", "Required"}});
  auto f = validate(in);
  auto reason = opt(in->changeReason);
  return m_db->tx([&](Conn& c) {
    auto cur = c.exec("SELECT *, date_of_birth::text AS dob FROM patients WHERE id = $1 FOR UPDATE", Params(id));
    if (cur.empty()) throw ApiError::notFound("Patient");
    auto row = cur.first();
    if (row.i64v("version") != *in->version)
      throw ApiError::conflict("STALE_VERSION", "This record was changed by someone else. Reload and try again.");

    std::vector<std::string> changed;
    for (auto& [col, val] : f.asList()) {
      std::optional<std::string> old = col == "date_of_birth" ? row.opt("dob")
                                       : col == "dob_is_estimated" ? std::optional<std::string>(row.boolv("dob_is_estimated") ? "true" : "false")
                                                                    : row.opt(col.c_str());
      if (old != val) {
        changed.push_back(col);
        c.exec("INSERT INTO patient_change_history (patient_id, changed_by, field_name, old_value, new_value, reason) "
               "VALUES ($1, $2, $3, $4, $5, $6)",
               Params(id, ctx.userId, col, old, val, reason));
      }
    }
    if (changed.empty()) return patientFromRow(c.exec(std::string(PATIENT_SELECT) + "WHERE p.id = $1", Params(id)).first());

    c.exec("UPDATE patients SET full_name=$2, date_of_birth=$3::date, dob_is_estimated=$4, gender=$5, mobile=$6, email=$7, "
           "address_line=$8, city=$9, district=$10, state=$11, pincode=$12, emergency_contact_name=$13, "
           "emergency_contact_relation=$14, emergency_contact_phone=$15, blood_group=$16, marital_status=$17, "
           "occupation=$18, preferred_language=$19, updated_by=$20, version = version + 1 WHERE id = $1",
           Params(id, f.fullName, f.dob, f.dobEstimated, f.gender, f.mobile, f.email, f.addressLine, f.city, f.district,
                  f.state, f.pincode, f.ecName, f.ecRelation, f.ecPhone, f.bloodGroup, f.maritalStatus, f.occupation,
                  f.language, ctx.userId));
    // Audit records which fields changed; values live in patient_change_history.
    m_audit->record(c, ctx, AuditEvent{"PATIENT_UPDATE", "patient", id, id, "SUCCESS",
                                       {{"changedFields", changed}, {"reason", reason.value_or("")}}});
    return patientFromRow(c.exec(std::string(PATIENT_SELECT) + "WHERE p.id = $1", Params(id)).first());
  });
}

std::string PatientService::normalizeAbhaNumber(const std::string& v) {
  std::string d;
  for (char ch : v)
    if (std::isdigit(static_cast<unsigned char>(ch))) d += ch;
  return d;
}
std::string PatientService::normalizeAbhaAddress(const std::string& v) { return text::lower(text::trim(v)); }

oatpp::Object<dto::PatientPageDto> PatientService::search(const AuthContext& ctx, const std::string& rawQ, int page,
                                                          int size) const {
  std::string qs = text::trim(rawQ);
  size = std::clamp(size, 1, 100);
  page = std::max(page, 1);
  std::string where = "WHERE p.is_active";
  Params p;
  std::string order = "p.created_at DESC";
  std::string kind = "all";
  if (!qs.empty()) {
    std::string digits = normalizeAbhaNumber(qs);
    static const std::regex numRe("^[A-Za-z]{2,6}-\\d{4}-\\d{1,6}$");
    if (std::regex_match(qs, numRe)) {
      kind = "patient_number";
      where += " AND upper(p.patient_number) = upper($1)";
      p.add(qs);
    } else if (digits.size() == 14 && qs.find_first_not_of("0123456789- ") == std::string::npos) {
      kind = "abha_number";
      where += " AND p.id IN (SELECT patient_id FROM patient_identifiers WHERE id_type = 'ABHA_NUMBER' "
               "AND value_hmac = $1 AND verification_status IN ('VERIFIED','PENDING'))";
      p.add(m_cipher->blindIndex("ABHA_NUMBER:" + digits));
    } else if (qs.find('@') != std::string::npos) {
      kind = "abha_address";
      where += " AND p.id IN (SELECT patient_id FROM patient_identifiers WHERE id_type = 'ABHA_ADDRESS' "
               "AND value_hmac = $1 AND verification_status IN ('VERIFIED','PENDING'))";
      p.add(m_cipher->blindIndex("ABHA_ADDRESS:" + normalizeAbhaAddress(qs)));
    } else if (!digits.empty() && digits.size() == qs.size()) {
      kind = "mobile";
      where += " AND p.mobile LIKE $1";
      p.add(digits + "%");
    } else {
      kind = "name";
      where += " AND (lower(p.full_name) LIKE '%' || lower($1) || '%' OR similarity(lower(p.full_name), lower($1)) > 0.3)";
      order = "similarity(lower(p.full_name), lower($1)) DESC, p.full_name";
      p.add(qs);
    }
  }
  return m_db->withConn([&](Conn& c) {
    auto total = c.exec("SELECT count(*) AS n FROM patients p " + where, p).first().i64v("n");
    int n = static_cast<int>(p.values.size());
    Params pp = p;
    pp.add(size);
    pp.add((page - 1) * size);
    auto rows = c.exec(std::string(PATIENT_SELECT) + where + " ORDER BY " + order + " LIMIT $" + std::to_string(n + 1) +
                           " OFFSET $" + std::to_string(n + 2),
                       pp);
    auto out = dto::PatientPageDto::createShared();
    out->items = oatpp::Vector<oatpp::Object<dto::PatientDto>>::createShared();
    rows.each([&](const Row& r) { out->items->push_back(patientFromRow(r)); });
    out->total = total;
    out->page = page;
    out->size = size;
    if (!qs.empty()) {
      // Access logging for searches: the kind of identifier used and result count (not the raw term).
      m_audit->recordStandalone(AuditActor::from(ctx), AuditEvent{"PATIENT_SEARCH", "patient", "", std::nullopt, "SUCCESS",
                                                                  {{"queryType", kind}, {"results", total}}});
    }
    return out;
  });
}

oatpp::Vector<oatpp::Object<dto::PatientChangeDto>> PatientService::history(const AuthContext& ctx, const std::string& id) const {
  return m_db->withConn([&](Conn& c) {
    requireExists(c, id);
    auto out = oatpp::Vector<oatpp::Object<dto::PatientChangeDto>>::createShared();
    c.exec("SELECT h.*, u.full_name AS by_name FROM patient_change_history h LEFT JOIN users u ON u.id = h.changed_by "
           "WHERE h.patient_id = $1 ORDER BY h.changed_at DESC, h.id DESC LIMIT 500",
           Params(id))
        .each([&](const Row& r) {
          auto d = dto::PatientChangeDto::createShared();
          d->id = r.i64("id");
          d->changedAt = r.ts("changed_at");
          d->changedBy = r.s("by_name");
          d->fieldName = r.s("field_name");
          d->oldValue = r.s("old_value");
          d->newValue = r.s("new_value");
          d->reason = r.s("reason");
          out->push_back(d);
        });
    return out;
  });
}

static oatpp::Object<dto::AllergyDto> allergyFromRow(const Row& r) {
  auto a = dto::AllergyDto::createShared();
  a->id = r.s("id");
  a->substance = r.s("substance");
  a->category = r.s("category");
  a->reaction = r.s("reaction");
  a->severity = r.s("severity");
  a->status = r.s("status");
  a->recordedBy = r.s("by_name");
  a->createdAt = r.ts("created_at");
  return a;
}

oatpp::Vector<oatpp::Object<dto::AllergyDto>> PatientService::allergies(const std::string& patientId) const {
  return m_db->withConn([&](Conn& c) {
    requireExists(c, patientId);
    auto out = oatpp::Vector<oatpp::Object<dto::AllergyDto>>::createShared();
    c.exec("SELECT a.*, u.full_name AS by_name FROM patient_allergies a LEFT JOIN users u ON u.id = a.recorded_by "
           "WHERE a.patient_id = $1 ORDER BY (a.status = 'ACTIVE') DESC, a.created_at DESC",
           Params(patientId))
        .each([&](const Row& r) { out->push_back(allergyFromRow(r)); });
    return out;
  });
}

oatpp::Object<dto::AllergyDto> PatientService::addAllergy(const AuthContext& ctx, const std::string& patientId,
                                                          const oatpp::Object<dto::AllergyInputDto>& in) {
  Validator v;
  std::string substance = trimmed(in->substance);
  std::string category = text::upper(trimmed(in->category));
  std::string severity = text::upper(trimmed(in->severity));
  if (severity.empty()) severity = "UNKNOWN";
  v.check(substance.size() >= 2 && substance.size() <= 120, "substance", "Substance is required");
  v.check(text::oneOf(category, {"MEDICATION", "FOOD", "ENVIRONMENT", "OTHER"}), "category", "Select a category");
  v.check(text::oneOf(severity, {"MILD", "MODERATE", "SEVERE", "UNKNOWN"}), "severity", "Select a severity");
  v.throwIfAny();
  return m_db->tx([&](Conn& c) {
    requireExists(c, patientId);
    auto id = c.exec("INSERT INTO patient_allergies (patient_id, substance, category, reaction, severity, recorded_by) "
                     "VALUES ($1,$2,$3,$4,$5,$6) RETURNING id",
                     Params(patientId, substance, category, opt(in->reaction), severity, ctx.userId))
                  .first()
                  .str("id");
    m_audit->record(c, ctx, AuditEvent{"ALLERGY_ADD", "allergy", id, patientId, "SUCCESS", {{"category", category}}});
    return allergyFromRow(c.exec("SELECT a.*, u.full_name AS by_name FROM patient_allergies a "
                                 "LEFT JOIN users u ON u.id = a.recorded_by WHERE a.id = $1", Params(id)).first());
  });
}

oatpp::Object<dto::AllergyDto> PatientService::updateAllergyStatus(const AuthContext& ctx, const std::string& patientId,
                                                                   const std::string& allergyId, const std::string& status) {
  std::string st = text::upper(status);
  if (!text::oneOf(st, {"ACTIVE", "INACTIVE", "ENTERED_IN_ERROR"}))
    throw ApiError::badRequest("Invalid status", {{"status", "ACTIVE, INACTIVE or ENTERED_IN_ERROR"}});
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("UPDATE patient_allergies SET status = $3 WHERE id = $1 AND patient_id = $2 RETURNING id",
                    Params(allergyId, patientId, st));
    if (r.empty()) throw ApiError::notFound("Allergy");
    m_audit->record(c, ctx, AuditEvent{"ALLERGY_STATUS_CHANGE", "allergy", allergyId, patientId, "SUCCESS", {{"status", st}}});
    return allergyFromRow(c.exec("SELECT a.*, u.full_name AS by_name FROM patient_allergies a "
                                 "LEFT JOIN users u ON u.id = a.recorded_by WHERE a.id = $1", Params(allergyId)).first());
  });
}

}  // namespace cf
