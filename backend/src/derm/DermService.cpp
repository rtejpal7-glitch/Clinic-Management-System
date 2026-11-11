#include "DermService.hpp"

#include <algorithm>
#include <map>
#include <regex>
#include <set>

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "derm/DermVocabulary.hpp"
#include "derm/PatternStats.hpp"

namespace cf {

using json = nlohmann::json;
using namespace derm;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
namespace {

std::vector<std::string> vec(const Row& r, const char* col) {
  std::vector<std::string> out;
  auto v = r.strArray(col);
  for (auto& s : *v)
    if (s) out.push_back(*s);
  return out;
}

std::string pgArr(const std::vector<std::string>& v) {
  std::string s = "{";
  for (size_t i = 0; i < v.size(); ++i) {
    s += (i ? "," : "") + std::string("\"");
    for (char ch : v[i]) {
      if (ch == '"' || ch == '\\') s += '\\';
      s += ch;
    }
    s += "\"";
  }
  return s + "}";
}

std::optional<std::string> jstr(const json& j, const char* k) {
  if (!j.contains(k) || j[k].is_null()) return std::nullopt;
  if (!j[k].is_string()) throw ApiError::badRequest(std::string("Field '") + k + "' must be text", {{k, "Must be text"}});
  std::string v = text::trim(j[k].get<std::string>());
  if (v.empty()) return std::nullopt;
  return v;
}
std::optional<bool> jbool(const json& j, const char* k) {
  if (!j.contains(k) || j[k].is_null()) return std::nullopt;
  if (!j[k].is_boolean()) throw ApiError::badRequest(std::string("Field '") + k + "' must be true/false", {{k, "Must be true or false"}});
  return j[k].get<bool>();
}
std::optional<double> jnum(const json& j, const char* k) {
  if (!j.contains(k) || j[k].is_null()) return std::nullopt;
  if (!j[k].is_number()) throw ApiError::badRequest(std::string("Field '") + k + "' must be a number", {{k, "Must be a number"}});
  return j[k].get<double>();
}
std::vector<std::string> jcodes(const json& j, const char* k, const Vocab& vocab, Validator& v) {
  std::vector<std::string> out;
  if (!j.contains(k) || j[k].is_null()) return out;
  if (!j[k].is_array()) {
    v.fail(k, "Must be a list");
    return out;
  }
  for (auto& x : j[k]) {
    if (!x.is_string() || !inVocab(vocab, x.get<std::string>())) {
      v.fail(k, "Contains an unknown code");
      continue;
    }
    if (std::find(out.begin(), out.end(), x.get<std::string>()) == out.end()) out.push_back(x.get<std::string>());
  }
  return out;
}
std::string optCode(const json& j, const char* k, const Vocab& vocab, Validator& v) {
  auto s = jstr(j, k);
  if (!s) return "";
  if (!inVocab(vocab, *s)) v.fail(k, "Unknown code");
  return *s;
}

json vocabJson(const Vocab& v) {
  json a = json::array();
  for (auto& [c, l] : v) a.push_back({{"code", c}, {"label", l}});
  return a;
}

const std::set<std::string> kSources = {"CLINICAL", "MY_PATTERNS", "NETWORK"};

}  // namespace

struct DermService::ConsultCtx {
  std::string id, patientId, doctorId, status, specialty, finalizedAt, facilityId;
  std::optional<int> ageYears;
  std::string sex, pregnancy, dxCode, dxDisplay;
  bool hasAssessment = false;
};

DermService::ConsultCtx DermService::loadConsult(Conn& c, const std::string& id) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Consultation");
  auto r = c.exec(
      "SELECT c.id, c.patient_id, c.doctor_id, c.status, c.specialty_code, c.finalized_at, c.facility_id, p.gender, "
      "  date_part('year', age(p.date_of_birth))::int AS age_years, da.pregnancy_status, (da.id IS NOT NULL) AS has_da, "
      "  dx.code AS dx_code, dx.display AS dx_display "
      "FROM consultations c JOIN patients p ON p.id = c.patient_id "
      "LEFT JOIN derm_assessments da ON da.consultation_id = c.id "
      "LEFT JOIN LATERAL (SELECT code, display FROM consultation_diagnoses d WHERE d.consultation_id = c.id "
      "  AND d.code_system = 'ICD-10' AND d.code IS NOT NULL "
      "  ORDER BY (d.diagnosis_type = 'PRIMARY') DESC, d.sort_order LIMIT 1) dx ON true "
      "WHERE c.id = $1",
      Params(id));
  if (r.empty()) throw ApiError::notFound("Consultation");
  auto row = r.first();
  ConsultCtx cc;
  cc.id = id;
  cc.patientId = row.str("patient_id");
  cc.doctorId = row.str("doctor_id");
  cc.status = row.str("status");
  cc.specialty = row.str("specialty_code");
  cc.finalizedAt = row.str("finalized_at");
  cc.facilityId = row.str("facility_id");
  cc.sex = row.str("gender");
  if (!row.isNull("age_years")) cc.ageYears = static_cast<int>(row.i64v("age_years"));
  cc.pregnancy = row.str("pregnancy_status");
  cc.hasAssessment = row.boolv("has_da");
  cc.dxCode = row.str("dx_code");
  cc.dxDisplay = row.str("dx_display");
  return cc;
}

static void requireTreatingDoctor(const AuthContext& ctx, const std::string& doctorId) {
  if (!ctx.doctorId || *ctx.doctorId != doctorId)
    throw ApiError::forbidden("Only the treating doctor can use decision support for this consultation");
}

DermFacts DermService::factsFor(Conn& c, const ConsultCtx& cc) const {
  DermFacts f;
  f.ageYears = cc.ageYears;
  auto r = c.exec("SELECT * FROM derm_assessments WHERE consultation_id = $1", Params(cc.id));
  if (r.empty()) return f;
  auto a = r.first();
  f.problemType = a.str("problem_type");
  std::optional<int> dv;
  if (!a.isNull("duration_value")) dv = static_cast<int>(a.i64v("duration_value"));
  f.durationDays = durationDays(dv, a.str("duration_unit"));
  f.severity = a.str("severity");
  for (auto& s : vec(a, "symptoms")) f.symptoms.insert(s);
  for (auto& s : vec(a, "body_sites")) f.sites.insert(s);
  for (auto& s : vec(a, "lesion_types")) f.lesions.insert(s);
  for (auto& s : vec(a, "colours")) f.colours.insert(s);
  for (auto& s : vec(a, "aggravating_factors")) f.aggravating.insert(s);
  f.configuration = a.str("configuration");
  f.distribution = a.str("distribution");
  if (!a.isNull("spreading")) f.spreading = a.boolv("spreading");
  if (!a.isNull("recurring")) f.recurring = a.boolv("recurring");
  if (!a.isNull("extent_bsa_percent")) f.extentPercent = std::stod(a.str("extent_bsa_percent"));
  std::string hist = text::lower(a.str("family_history") + " " + a.str("other_conditions"));
  for (auto kw : {"asthma", "eczema", "atop", "rhinitis", "hay fever"})
    if (hist.find(kw) != std::string::npos) f.historyAtopy = true;
  return f;
}

// ---------------------------------------------------------------------------
// vocabulary & assessment
// ---------------------------------------------------------------------------
json DermService::vocabulary() {
  return {{"problemTypes", vocabJson(problemTypes())}, {"symptoms", vocabJson(symptoms())},
          {"bodySites", vocabJson(bodySites())},       {"lesionTypes", vocabJson(lesionTypes())},
          {"colours", vocabJson(colours())},           {"configurations", vocabJson(configurations())},
          {"distributions", vocabJson(distributions())}, {"factors", vocabJson(factors())},
          {"severities", json::array({{{"code", "MILD"}, {"label", "Mild"}}, {{"code", "MODERATE"}, {"label", "Moderate"}},
                                      {{"code", "SEVERE"}, {"label", "Severe"}}})},
          {"durationUnits", json::array({"DAYS", "WEEKS", "MONTHS", "YEARS"})},
          {"pregnancyStatuses", json::array({"NOT_APPLICABLE", "NOT_PREGNANT", "PREGNANT", "BREASTFEEDING", "UNKNOWN"})},
          {"treatmentResponses", json::array({"GOOD", "PARTIAL", "NONE", "WORSENED", "NOT_TRIED"})},
          {"outcomes", json::array({"RESOLVED", "IMPROVED", "UNCHANGED", "WORSENED"})}};
}

static json assessmentJson(const Row& a) {
  json j = {{"exists", true},
            {"problemType", a.isNull("problem_type") ? json(nullptr) : json(a.str("problem_type"))},
            {"durationValue", a.isNull("duration_value") ? json(nullptr) : json(a.i64v("duration_value"))},
            {"durationUnit", a.isNull("duration_unit") ? json(nullptr) : json(a.str("duration_unit"))},
            {"severity", a.isNull("severity") ? json(nullptr) : json(a.str("severity"))},
            {"symptoms", vec(a, "symptoms")},
            {"spreading", a.isNull("spreading") ? json(nullptr) : json(a.boolv("spreading"))},
            {"recurring", a.isNull("recurring") ? json(nullptr) : json(a.boolv("recurring"))},
            {"previousEpisodes", a.isNull("previous_episodes") ? json(nullptr) : json(a.i64v("previous_episodes"))},
            {"aggravatingFactors", vec(a, "aggravating_factors")},
            {"relievingFactors", vec(a, "relieving_factors")},
            {"bodySites", vec(a, "body_sites")},
            {"lesionTypes", vec(a, "lesion_types")},
            {"colours", vec(a, "colours")},
            {"configuration", a.isNull("configuration") ? json(nullptr) : json(a.str("configuration"))},
            {"distribution", a.isNull("distribution") ? json(nullptr) : json(a.str("distribution"))},
            {"extentBsaPercent", a.isNull("extent_bsa_percent") ? json(nullptr) : json(std::stod(a.str("extent_bsa_percent")))},
            {"findingsNotes", a.isNull("findings_notes") ? json(nullptr) : json(a.str("findings_notes"))},
            {"otherConditions", a.isNull("other_conditions") ? json(nullptr) : json(a.str("other_conditions"))},
            {"previousDermTreatments", a.isNull("previous_derm_treatments") ? json(nullptr) : json(a.str("previous_derm_treatments"))},
            {"previousTreatmentResponse", a.isNull("previous_treatment_response") ? json(nullptr) : json(a.str("previous_treatment_response"))},
            {"familyHistory", a.isNull("family_history") ? json(nullptr) : json(a.str("family_history"))},
            {"pregnancyStatus", a.isNull("pregnancy_status") ? json(nullptr) : json(a.str("pregnancy_status"))},
            {"updatedAt", *a.ts("updated_at")}};
  return j;
}

json DermService::getAssessment(const AuthContext& ctx, const std::string& id) const {
  return m_db->withConn([&](Conn& c) {
    auto cc = loadConsult(c, id);
    auto r = c.exec("SELECT * FROM derm_assessments WHERE consultation_id = $1", Params(id));
    json j = r.empty() ? json{{"exists", false}} : assessmentJson(r.first());
    j["consultationId"] = id;
    j["specialtyCode"] = cc.specialty;
    j["canEdit"] = cc.status == "DRAFT" && ctx.doctorId && *ctx.doctorId == cc.doctorId && ctx.has(perm::CLINICAL_WRITE);
    return j;
  });
}

json DermService::saveAssessment(const AuthContext& ctx, const std::string& id, const json& in) {
  if (!in.is_object()) throw ApiError::badRequest("Request body must be a JSON object");
  Validator v;
  std::string problem = optCode(in, "problemType", problemTypes(), v);
  auto dval = jnum(in, "durationValue");
  auto dunit = jstr(in, "durationUnit");
  if (dval) v.check(*dval >= 0 && *dval <= 1000, "durationValue", "0 to 1000");
  if (dunit) v.check(text::oneOf(*dunit, {"DAYS", "WEEKS", "MONTHS", "YEARS"}), "durationUnit", "DAYS, WEEKS, MONTHS or YEARS");
  if (dval && !dunit) v.fail("durationUnit", "Select a unit");
  auto severity = jstr(in, "severity");
  if (severity) v.check(text::oneOf(*severity, {"MILD", "MODERATE", "SEVERE"}), "severity", "MILD, MODERATE or SEVERE");
  auto sym = jcodes(in, "symptoms", symptoms(), v);
  auto agg = jcodes(in, "aggravatingFactors", factors(), v);
  auto rel = jcodes(in, "relievingFactors", factors(), v);
  auto sites = jcodes(in, "bodySites", bodySites(), v);
  auto lesions = jcodes(in, "lesionTypes", lesionTypes(), v);
  auto cols = jcodes(in, "colours", colours(), v);
  std::string config = optCode(in, "configuration", configurations(), v);
  std::string dist = optCode(in, "distribution", distributions(), v);
  auto extent = jnum(in, "extentBsaPercent");
  if (extent) v.check(*extent >= 0 && *extent <= 100, "extentBsaPercent", "0 to 100");
  auto prevEp = jnum(in, "previousEpisodes");
  if (prevEp) v.check(*prevEp >= 0 && *prevEp <= 100, "previousEpisodes", "0 to 100");
  auto resp = jstr(in, "previousTreatmentResponse");
  if (resp) v.check(text::oneOf(*resp, {"GOOD", "PARTIAL", "NONE", "WORSENED", "NOT_TRIED"}), "previousTreatmentResponse", "Invalid value");
  auto preg = jstr(in, "pregnancyStatus");
  if (preg) v.check(text::oneOf(*preg, {"NOT_APPLICABLE", "NOT_PREGNANT", "PREGNANT", "BREASTFEEDING", "UNKNOWN"}), "pregnancyStatus", "Invalid value");
  std::map<std::string, std::optional<std::string>> texts;
  for (auto k : {"findingsNotes", "otherConditions", "previousDermTreatments", "familyHistory"}) {
    texts[k] = jstr(in, k);
    if (texts[k] && texts[k]->size() > 4000) v.fail(k, "Limited to 4000 characters");
  }
  v.throwIfAny("Dermatology assessment has invalid values");

  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    if (!ctx.doctorId || *ctx.doctorId != cc.doctorId) throw ApiError::forbidden("Only the treating doctor can edit this consultation");
    if (cc.status != "DRAFT") throw ApiError::conflict("RECORD_LOCKED", "Finalized consultations cannot be edited. Add an addendum instead.");
    if (cc.sex == "MALE" && preg && (*preg == "PREGNANT" || *preg == "BREASTFEEDING"))
      throw ApiError::badRequest("Pregnancy status does not match the patient's recorded sex", {{"pregnancyStatus", "Check value"}});
    auto opt = [](const std::string& s) { return s.empty() ? std::optional<std::string>() : std::optional<std::string>(s); };
    auto optNum = [](const std::optional<double>& d, bool integer) {
      if (!d) return std::optional<std::string>();
      return std::optional<std::string>(integer ? std::to_string(static_cast<long long>(*d)) : std::to_string(*d));
    };
    auto optBool = [](const std::optional<bool>& b) {
      if (!b) return std::optional<std::string>();
      return std::optional<std::string>(*b ? "true" : "false");
    };
    c.exec("INSERT INTO derm_assessments (consultation_id, patient_id, problem_type, duration_value, duration_unit, severity, symptoms, "
           "  spreading, recurring, previous_episodes, aggravating_factors, relieving_factors, body_sites, lesion_types, colours, "
           "  configuration, distribution, extent_bsa_percent, findings_notes, other_conditions, previous_derm_treatments, "
           "  previous_treatment_response, family_history, pregnancy_status, updated_by) "
           "VALUES ($1,$2,$3,$4::int,$5,$6,$7::text[],$8::boolean,$9::boolean,$10::int,$11::text[],$12::text[],$13::text[],$14::text[],"
           "  $15::text[],$16,$17,$18::numeric,$19,$20,$21,$22,$23,$24,$25) "
           "ON CONFLICT (consultation_id) DO UPDATE SET problem_type = EXCLUDED.problem_type, duration_value = EXCLUDED.duration_value, "
           "  duration_unit = EXCLUDED.duration_unit, severity = EXCLUDED.severity, symptoms = EXCLUDED.symptoms, "
           "  spreading = EXCLUDED.spreading, recurring = EXCLUDED.recurring, previous_episodes = EXCLUDED.previous_episodes, "
           "  aggravating_factors = EXCLUDED.aggravating_factors, relieving_factors = EXCLUDED.relieving_factors, "
           "  body_sites = EXCLUDED.body_sites, lesion_types = EXCLUDED.lesion_types, colours = EXCLUDED.colours, "
           "  configuration = EXCLUDED.configuration, distribution = EXCLUDED.distribution, "
           "  extent_bsa_percent = EXCLUDED.extent_bsa_percent, findings_notes = EXCLUDED.findings_notes, "
           "  other_conditions = EXCLUDED.other_conditions, previous_derm_treatments = EXCLUDED.previous_derm_treatments, "
           "  previous_treatment_response = EXCLUDED.previous_treatment_response, family_history = EXCLUDED.family_history, "
           "  pregnancy_status = EXCLUDED.pregnancy_status, updated_by = EXCLUDED.updated_by, updated_at = now()",
           Params(id, cc.patientId, opt(problem), optNum(dval, true), dunit, severity, pgArr(sym), optBool(jbool(in, "spreading")),
                  optBool(jbool(in, "recurring")), optNum(prevEp, true), pgArr(agg), pgArr(rel), pgArr(sites), pgArr(lesions),
                  pgArr(cols), opt(config), opt(dist), optNum(extent, false), texts["findingsNotes"], texts["otherConditions"],
                  texts["previousDermTreatments"], resp, texts["familyHistory"], preg, ctx.userId));
    c.exec("UPDATE consultations SET specialty_code = 'DERMATOLOGY', version = version + 1 WHERE id = $1 AND specialty_code <> 'DERMATOLOGY'",
           Params(id));
    m_audit->record(c, ctx, AuditEvent{"DERM_ASSESSMENT_UPDATE", "consultation", id, cc.patientId, "SUCCESS", {}});
    auto r = c.exec("SELECT * FROM derm_assessments WHERE consultation_id = $1", Params(id));
    json j = assessmentJson(r.first());
    j["consultationId"] = id;
    j["canEdit"] = true;
    return j;
  });
}

// ---------------------------------------------------------------------------
// photographs
// ---------------------------------------------------------------------------
static std::string sniffImage(const std::string& b) {
  auto u = [&](size_t i) { return static_cast<unsigned char>(b[i]); };
  if (b.size() >= 3 && u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) return "image/jpeg";
  if (b.size() >= 8 && u(0) == 0x89 && b.compare(1, 3, "PNG") == 0) return "image/png";
  if (b.size() >= 12 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return "image/webp";
  return "";
}

static json photoJson(const Row& r) {
  return {{"id", r.str("id")}, {"consultationId", r.str("consultation_id")},
          {"bodySite", r.isNull("body_site") ? json(nullptr) : json(r.str("body_site"))},
          {"caption", r.isNull("caption") ? json(nullptr) : json(r.str("caption"))},
          {"contentType", r.str("content_type")}, {"sizeBytes", r.i64v("size_bytes")},
          {"consentScope", r.str("consent_scope")}, {"status", r.str("status")},
          {"statusReason", r.isNull("status_reason") ? json(nullptr) : json(r.str("status_reason"))},
          {"capturedBy", r.str("captured_by_name")}, {"capturedAt", *r.ts("captured_at")}};
}

json DermService::listPhotos(const AuthContext& ctx, const std::string& id) const {
  return m_db->withConn([&](Conn& c) {
    loadConsult(c, id);
    json out = json::array();
    c.exec("SELECT p.id, p.consultation_id, p.body_site, p.caption, p.content_type, p.size_bytes, p.consent_scope, p.status, "
           "  p.status_reason, p.captured_at, u.full_name AS captured_by_name "
           "FROM clinical_photos p JOIN users u ON u.id = p.captured_by WHERE p.consultation_id = $1 ORDER BY p.captured_at",
           Params(id))
        .each([&](const Row& r) { out.push_back(photoJson(r)); });
    return out;
  });
}

json DermService::uploadPhoto(const AuthContext& ctx, const std::string& id, const json& in) {
  if (!in.is_object()) throw ApiError::badRequest("Request body must be a JSON object");
  Validator v;
  auto consent = jbool(in, "consentObtained");
  v.check(consent && *consent, "consentObtained", "Patient consent is required before taking clinical photographs");
  auto scope = jstr(in, "consentScope").value_or("CLINICAL_RECORD_ONLY");
  v.check(text::oneOf(scope, {"CLINICAL_RECORD_ONLY", "CLINICAL_RECORD_AND_TEACHING"}), "consentScope", "Invalid consent scope");
  auto site = jstr(in, "bodySite");
  if (site) v.check(inVocab(bodySites(), *site), "bodySite", "Unknown body site");
  auto caption = jstr(in, "caption");
  if (caption) v.check(caption->size() <= 200, "caption", "At most 200 characters");
  auto data = jstr(in, "dataBase64");
  v.check(data.has_value(), "dataBase64", "Image data is required");
  v.throwIfAny("Photo could not be accepted");
  std::string b64 = *data;
  auto comma = b64.find(',');
  if (b64.rfind("data:", 0) == 0 && comma != std::string::npos) b64 = b64.substr(comma + 1);
  std::string bytes;
  try {
    bytes = crypto::base64Decode(b64);
  } catch (...) {
    throw ApiError::badRequest("Image data is not valid base64", {{"dataBase64", "Invalid encoding"}});
  }
  if (bytes.empty() || bytes.size() > 6 * 1024 * 1024)
    throw ApiError::badRequest("Image must be between 1 byte and 6 MB", {{"dataBase64", "Too large"}});
  std::string type = sniffImage(bytes);
  if (type.empty()) throw ApiError::badRequest("Only JPEG, PNG or WebP images are accepted", {{"dataBase64", "Unsupported image type"}});

  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    if (!ctx.doctorId || *ctx.doctorId != cc.doctorId) throw ApiError::forbidden("Only the treating doctor can add photographs");
    if (cc.status != "DRAFT") throw ApiError::conflict("RECORD_LOCKED", "Photographs can only be added while the consultation is a draft");
    auto n = c.exec("SELECT count(*) AS n FROM clinical_photos WHERE consultation_id = $1 AND status = 'ACTIVE'", Params(id)).first().i64v("n");
    if (n >= 12) throw ApiError::conflict("PHOTO_LIMIT", "At most 12 photographs per consultation");
    auto pid = c.exec("INSERT INTO clinical_photos (consultation_id, patient_id, body_site, caption, content_type, size_bytes, sha256, "
                      "  image_enc, consent_obtained, consent_scope, captured_by) "
                      "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,true,$9,$10) RETURNING id",
                      Params(id, cc.patientId, site, caption, type, static_cast<long long>(bytes.size()), crypto::sha256Hex(bytes),
                             m_cipher->encrypt(bytes), scope, ctx.userId))
                   .first()
                   .str("id");
    m_audit->record(c, ctx, AuditEvent{"PHOTO_CAPTURE", "clinical_photo", pid, cc.patientId, "SUCCESS",
                                       {{"consultationId", id}, {"consentScope", scope}, {"bytes", (long long)bytes.size()}}});
    auto r = c.exec("SELECT p.*, u.full_name AS captured_by_name FROM clinical_photos p JOIN users u ON u.id = p.captured_by "
                    "WHERE p.id = $1", Params(pid));
    return photoJson(r.first());
  });
}

std::pair<std::string, std::string> DermService::photoImage(const AuthContext& ctx, const std::string& photoId) const {
  if (!text::isUuid(photoId)) throw ApiError::notFound("Photo");
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("SELECT patient_id, content_type, image_enc, status FROM clinical_photos WHERE id = $1", Params(photoId));
    if (r.empty()) throw ApiError::notFound("Photo");
    auto row = r.first();
    m_audit->record(c, ctx, AuditEvent{"PHOTO_VIEW", "clinical_photo", photoId, row.str("patient_id"), "SUCCESS",
                                       {{"status", row.str("status")}}});
    return std::make_pair(row.str("content_type"), m_cipher->decrypt(row.str("image_enc")));
  });
}

json DermService::markPhotoEnteredInError(const AuthContext& ctx, const std::string& photoId, const json& in) {
  auto reason = jstr(in, "reason");
  if (!reason || reason->size() < 3) throw ApiError::badRequest("A reason is required", {{"reason", "Required"}});
  if (!text::isUuid(photoId)) throw ApiError::notFound("Photo");
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("SELECT p.patient_id, c.doctor_id FROM clinical_photos p JOIN consultations c ON c.id = p.consultation_id "
                    "WHERE p.id = $1 FOR UPDATE OF p", Params(photoId));
    if (r.empty()) throw ApiError::notFound("Photo");
    if (!ctx.doctorId || *ctx.doctorId != r.first().str("doctor_id"))
      throw ApiError::forbidden("Only the treating doctor can mark this photograph");
    c.exec("UPDATE clinical_photos SET status = 'ENTERED_IN_ERROR', status_reason = $2 WHERE id = $1", Params(photoId, *reason));
    m_audit->record(c, ctx, AuditEvent{"PHOTO_ENTERED_IN_ERROR", "clinical_photo", photoId, r.first().str("patient_id"), "SUCCESS",
                                       {{"reason", *reason}}});
    auto p = c.exec("SELECT p.*, u.full_name AS captured_by_name FROM clinical_photos p JOIN users u ON u.id = p.captured_by "
                    "WHERE p.id = $1", Params(photoId));
    return photoJson(p.first());
  });
}

// ---------------------------------------------------------------------------
// catalogue helpers & safety
// ---------------------------------------------------------------------------
json DermService::catalogueProducts(Conn& c, const std::string& generic) const {
  json out = json::array();
  c.exec("SELECT id, generic_name, brand_name, strength, dosage_form, default_route, review_status, default_dose, default_frequency, "
         "  default_duration_days, common_dosage, high_risk FROM medication_catalogue "
         "WHERE generic_code = $1 AND is_active ORDER BY (brand_name IS NULL) DESC, brand_name LIMIT 6",
         Params(generic))
      .each([&](const Row& r) {
        bool approved = r.str("review_status") == "APPROVED";
        json p = {{"medicationId", r.str("id")}, {"genericName", r.str("generic_name")},
                  {"brandName", r.isNull("brand_name") ? json(nullptr) : json(r.str("brand_name"))},
                  {"strength", r.str("strength")}, {"dosageForm", r.str("dosage_form")}, {"route", r.str("default_route")},
                  {"reviewStatus", r.str("review_status")}, {"highRisk", r.boolv("high_risk")}};
        if (approved) {
          p["commonDosage"] = r.isNull("common_dosage") ? json(nullptr) : json(r.str("common_dosage"));
          p["defaultDose"] = r.isNull("default_dose") ? json(nullptr) : json(r.str("default_dose"));
          p["defaultFrequency"] = r.isNull("default_frequency") ? json(nullptr) : json(r.str("default_frequency"));
          p["defaultDurationDays"] = r.isNull("default_duration_days") ? json(nullptr) : json(r.i64v("default_duration_days"));
        }
        out.push_back(p);
      });
  return out;
}

static SafetyPatient patientFor(Conn& c, const std::string& patientId, const std::optional<int>& age, const std::string& sex,
                                const std::string& pregnancy) {
  SafetyPatient p;
  p.ageYears = age;
  p.sex = sex;
  p.pregnancy = pregnancy;
  c.exec("SELECT substance FROM patient_allergies WHERE patient_id = $1 AND status = 'ACTIVE' AND category IN ('MEDICATION','OTHER')",
         Params(patientId))
      .each([&](const Row& r) { p.allergies.push_back(r.str("substance")); });
  return p;
}

static std::vector<SafetyInteraction> loadInteractions(Conn& c) {
  std::vector<SafetyInteraction> v;
  c.exec("SELECT * FROM medication_interactions WHERE review_status <> 'RETIRED'").each([&](const Row& r) {
    v.push_back({r.str("generic_a"), r.str("generic_b"), r.str("severity"), r.str("description"), r.str("reference"),
                 r.str("review_status") == "APPROVED"});
  });
  return v;
}

static SafetyMed medFromCatalogueRow(const Row& r, int index, const std::string& displayName) {
  SafetyMed m;
  m.index = index;
  m.name = displayName;
  m.catalogued = !r.isNull("cat_id");
  if (!m.catalogued) return m;
  m.genericCode = r.str("generic_code");
  m.ingredients = vec(r, "ingredient_codes");
  if (m.ingredients.empty() && !m.genericCode.empty()) m.ingredients.push_back(m.genericCode);
  m.reviewed = r.str("review_status") == "APPROVED";
  bool retired = r.str("review_status") == "RETIRED";
  m.rules = retired ? json::array() : json::parse(r.str("contraindication_rules", "[]"));
  m.highRisk = r.boolv("high_risk");
  m.restricted = r.boolv("restricted_antimicrobial");
  m.aware = r.str("aware_category");
  return m;
}

json DermService::safetyAlerts(Conn& c, const ConsultCtx& cc) const {
  auto patient = patientFor(c, cc.patientId, cc.ageYears, cc.sex, cc.pregnancy);
  std::vector<SafetyMed> meds;
  int i = 0;
  c.exec("SELECT i.medicine_name, i.strength, m.id AS cat_id, m.generic_code, m.ingredient_codes, m.review_status, "
         "  m.contraindication_rules::text AS contraindication_rules, m.high_risk, m.restricted_antimicrobial, m.aware_category "
         "FROM prescription_items i JOIN prescriptions rx ON rx.id = i.prescription_id "
         "LEFT JOIN medication_catalogue m ON m.id = i.medication_id WHERE rx.consultation_id = $1 ORDER BY i.sort_order",
         Params(cc.id))
      .each([&](const Row& r) {
        meds.push_back(medFromCatalogueRow(r, i++, r.str("medicine_name") + (r.isNull("strength") ? "" : " " + r.str("strength"))));
      });
  return checkPrescription(patient, meds, loadInteractions(c));
}

json DermService::annotateForPatient(Conn& c, const ConsultCtx& cc, const std::string& generic) const {
  auto r = c.exec("SELECT id AS cat_id, generic_name, generic_code, ingredient_codes, review_status, "
                  "  contraindication_rules::text AS contraindication_rules, high_risk, restricted_antimicrobial, aware_category "
                  "FROM medication_catalogue WHERE generic_code = $1 ORDER BY (brand_name IS NULL) DESC LIMIT 1",
                  Params(generic));
  if (r.empty()) return json::array();
  auto patient = patientFor(c, cc.patientId, cc.ageYears, cc.sex, cc.pregnancy);
  auto med = medFromCatalogueRow(r.first(), 0, r.first().str("generic_name"));
  json all = checkPrescription(patient, {med}, {});
  json out = json::array();
  for (auto& a : all)
    if (a["level"] != "INFO" || a["code"] == "PREGNANCY_STATUS_UNKNOWN") out.push_back(a);
  return out;
}

json DermService::safetyCheck(const AuthContext& ctx, const std::string& id) const {
  return m_db->withConn([&](Conn& c) {
    auto cc = loadConsult(c, id);
    json alerts = safetyAlerts(c, cc);
    return json{{"alerts", alerts}, {"requiresOverride", requiresOverride(alerts)},
                {"note", "Automated checks cover recorded allergies, pregnancy/breastfeeding, age, duplicate ingredients and "
                         "known interactions in the catalogue. They do not replace clinical judgement or a full drug reference."}};
  });
}

std::string DermService::snapshot(Conn& c, const AuthContext& ctx, const std::string& id, const std::string& source,
                                  const json& payload, const std::vector<std::string>& generics) const {
  return c.exec("INSERT INTO suggestion_snapshots (consultation_id, source, status, payload, suggested_generics, created_by) "
                "VALUES ($1,$2,$3,$4::jsonb,$5::text[],$6) RETURNING id",
                Params(id, source, payload.value("status", "OK"), payload.dump(), pgArr(generics), ctx.userId))
      .first()
      .str("id");
}

// ---------------------------------------------------------------------------
// Panel A — clinical knowledge
// ---------------------------------------------------------------------------
json DermService::clinicalPanel(const AuthContext& ctx, const std::string& id) {
  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    requireTreatingDoctor(ctx, cc.doctorId);
    auto facts = factsFor(c, cc);

    std::vector<CdsCondition> conditions;
    std::map<std::string, size_t> idx;
    c.exec("SELECT id, code, name, icd10_code, summary, refs::text AS refs, content_version FROM cds_conditions "
           "WHERE review_status = 'APPROVED' AND specialty = 'DERMATOLOGY' ORDER BY code")
        .each([&](const Row& r) {
          CdsCondition k;
          k.code = r.str("code");
          k.name = r.str("name");
          k.icd10 = r.str("icd10_code");
          k.summary = r.str("summary");
          k.refs = json::parse(r.str("refs", "[]"));
          k.contentVersion = static_cast<int>(r.i64v("content_version", 1));
          idx[r.str("id")] = conditions.size();
          conditions.push_back(k);
        });
    if (!conditions.empty()) {
      c.exec("SELECT f.condition_id, f.feature, f.weight FROM cds_condition_features f JOIN cds_conditions k ON k.id = f.condition_id "
             "WHERE k.review_status = 'APPROVED'")
          .each([&](const Row& r) {
            conditions[idx[r.str("condition_id")]].features.push_back({r.str("feature"), static_cast<int>(r.i64v("weight"))});
          });
      c.exec("SELECT x.* FROM cds_condition_content x JOIN cds_conditions k ON k.id = x.condition_id "
             "WHERE k.review_status = 'APPROVED' ORDER BY x.sort_order")
          .each([&](const Row& r) {
            CdsContentItem it{r.str("kind"), r.str("text"), vec(r, "generic_codes"), r.str("line"), vec(r, "severities"), r.str("reference")};
            conditions[idx[r.str("condition_id")]].content.push_back(it);
          });
    }
    std::vector<CdsRedFlag> flags;
    c.exec("SELECT code, title, guidance, reference, criteria::text AS criteria FROM cds_red_flags WHERE review_status = 'APPROVED'")
        .each([&](const Row& r) {
          flags.push_back({r.str("code"), r.str("title"), r.str("guidance"), r.str("reference"), json::parse(r.str("criteria"))});
        });
    int pending = static_cast<int>(
        c.exec("SELECT (SELECT count(*) FROM cds_conditions WHERE review_status = 'UNREVIEWED') + "
               "(SELECT count(*) FROM cds_red_flags WHERE review_status = 'UNREVIEWED') AS n")
            .first()
            .i64v("n"));

    json result = evaluate(facts, conditions, flags, pending);
    result["hasAssessment"] = cc.hasAssessment;
    // Link treatment options to catalogue products and check them against this patient.
    std::vector<std::string> generics;
    json suggestions = json::array();
    for (auto& t : result["treatments"]) {
      json meds = json::array();
      for (auto& g : t["generics"]) {
        std::string code = g.get<std::string>();
        auto products = catalogueProducts(c, code);
        json first = products.empty() ? json(nullptr) : products[0];
        meds.push_back({{"generic", code}, {"products", products}, {"patientAlerts", annotateForPatient(c, cc, code)}});
        if (std::find(generics.begin(), generics.end(), code) == generics.end()) {
          generics.push_back(code);
          json s = {{"generic", code}};
          if (!first.is_null() && first.contains("defaultDose")) {
            s["dose"] = first["defaultDose"];
            s["frequency"] = first["defaultFrequency"];
            s["durationDays"] = first["defaultDurationDays"];
          }
          suggestions.push_back(s);
        }
      }
      t["medicines"] = meds;
    }
    result["suggestions"] = suggestions;
    result["disclaimer"] =
        "Decision support only. Possible conditions are based on the findings recorded and approved knowledge-base content; "
        "they are not a diagnosis. Treatment options are summaries of the cited guidelines and must be checked against the "
        "patient and current prescribing information. The doctor makes all diagnostic and prescribing decisions.";
    json input = {{"facts", result["recordedFindings"]}, {"severity", facts.severity}, {"ageYears", facts.ageYears ? json(*facts.ageYears) : json(nullptr)}};
    c.exec("INSERT INTO cds_analysis_runs (consultation_id, engine_version, status, input, result, created_by) "
           "VALUES ($1,$2,$3,$4::jsonb,$5::jsonb,$6)",
           Params(id, std::string(CDS_ENGINE_VERSION),
                  result["status"] == "NO_MATCH" ? std::string("OK") : result["status"].get<std::string>(), input.dump(),
                  result.dump(), ctx.userId));
    result["snapshotId"] = snapshot(c, ctx, id, "CLINICAL", result, generics);
    m_audit->record(c, ctx, AuditEvent{"CDS_CLINICAL_VIEW", "consultation", id, cc.patientId, "SUCCESS",
                                       {{"status", result["status"]}, {"conditions", result["conditions"].size()},
                                        {"redFlags", result["redFlags"].size()}}});
    return result;
  });
}

// ---------------------------------------------------------------------------
// Panel B — the doctor's own prescribing patterns
// ---------------------------------------------------------------------------
namespace {
struct Tier {
  const char* name;
  const char* label;
  bool severity;
  bool age;
};
const Tier kTiers[] = {{"DX_SEVERITY_AGE", "Same diagnosis, severity and age group", true, true},
                       {"DX_SEVERITY", "Same diagnosis and severity", true, false},
                       {"DX", "Same diagnosis", false, false}};

std::string pregnancyClause(const std::string& pregnancy, int paramNo) {
  // Pregnant patients are only compared with pregnant cases; everyone else excludes them.
  (void)paramNo;
  return pregnancy == "PREGNANT" ? " AND pregnancy_status = 'PREGNANT'" : " AND coalesce(pregnancy_status, '') <> 'PREGNANT'";
}
}  // namespace

json DermService::myPatternsPanel(const AuthContext& ctx, const std::string& id) {
  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    requireTreatingDoctor(ctx, cc.doctorId);
    auto facts = factsFor(c, cc);
    json out = {{"source", "MY_PATTERNS"}};
    out["disclaimer"] = "What you prescribed in your own previous comparable cases. Frequency shows your habits, not effectiveness; "
                        "outcomes are only those recorded at follow-up and are not controlled comparisons.";
    if (cc.dxCode.empty()) {
      out["status"] = "INSUFFICIENT_DATA";
      out["message"] = "Record a coded (ICD-10) working diagnosis to find your comparable previous cases.";
      out["snapshotId"] = snapshot(c, ctx, id, "MY_PATTERNS", out, {});
      return out;
    }
    std::string ageBandV = cc.ageYears ? ageBand(*cc.ageYears) : "";
    std::vector<PatternCase> cases;
    std::string tierUsed;
    for (auto& t : kTiers) {
      if (t.severity && facts.severity.empty()) continue;
      if (t.age && ageBandV.empty()) continue;
      std::string sql = "SELECT generic_codes, items::text AS items, coalesce(outcome, '') AS outcome, finalized_at::date::text AS d "
                        "FROM prescribing_cases WHERE doctor_id = $1 AND consultation_id <> $2 AND dx_code = $3";
      Params p(*ctx.doctorId, id, cc.dxCode);
      int n = 3;
      if (t.severity) { sql += " AND severity = $" + std::to_string(++n); p.add(facts.severity); }
      if (t.age) { sql += " AND age_band = $" + std::to_string(++n); p.add(ageBandV); }
      sql += pregnancyClause(cc.pregnancy, 0) + " ORDER BY finalized_at DESC LIMIT 500";
      cases.clear();
      c.exec(sql, p).each([&](const Row& r) {
        PatternCase pc;
        pc.generics = vec(r, "generic_codes");
        pc.items = json::parse(r.str("items", "[]"));
        pc.outcome = r.str("outcome");
        pc.doctorKey = "me";
        pc.facilityKey = "mine";
        pc.period = r.str("d");
        cases.push_back(pc);
      });
      tierUsed = t.name;
      if (static_cast<int>(cases.size()) >= OwnThresholds{}.minCases) {
        out["comparison"] = {{"tier", t.name}, {"description", t.label}};
        break;
      }
    }
    json s = summarizeOwn(cases);
    for (auto& [k, v] : s.items()) out[k] = v;
    out["diagnosis"] = {{"code", cc.dxCode}, {"display", cc.dxDisplay}};
    if (!out.contains("comparison")) out["comparison"] = {{"tier", tierUsed}, {"description", "Same diagnosis"}};
    out["comparison"]["severity"] = facts.severity;
    out["comparison"]["ageBand"] = ageBandV;
    out["comparison"]["pregnancyCompatible"] = true;
    std::vector<std::string> generics;
    json suggestions = json::array();
    for (auto& m : out["medications"]) {
      std::string g = m["generic"];
      generics.push_back(g);
      m["products"] = catalogueProducts(c, g);
      m["patientAlerts"] = annotateForPatient(c, cc, g);
      auto& reg = m["usualRegimen"];
      suggestions.push_back({{"generic", g}, {"route", reg.value("route", "")}, {"frequency", reg.value("frequency", "")},
                             {"durationDays", reg.contains("durationDays") ? reg["durationDays"] : json(nullptr)}});
    }
    out["suggestions"] = suggestions;
    out["snapshotId"] = snapshot(c, ctx, id, "MY_PATTERNS", out, generics);
    m_audit->record(c, ctx, AuditEvent{"CDS_MY_PATTERNS_VIEW", "consultation", id, cc.patientId, "SUCCESS",
                                       {{"status", out["status"]}, {"cases", out.value("caseCount", 0)}}});
    return out;
  });
}

// ---------------------------------------------------------------------------
// Panel C — ClinicFlow network patterns
// ---------------------------------------------------------------------------
json DermService::networkPanel(const AuthContext& ctx, const std::string& id) {
  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    requireTreatingDoctor(ctx, cc.doctorId);
    auto facts = factsFor(c, cc);
    json out = {{"source", "NETWORK"}};
    out["disclaimer"] =
        "De-identified prescribing patterns of other doctors at ClinicFlow clinics that take part in the network. Not "
        "representative of all dermatologists; how often a medicine is prescribed does not show that it works. No individual "
        "doctor, clinic or patient can be identified.";
    auto fac = c.exec("SELECT network_sharing_enabled FROM facilities WHERE id = cf_current_facility()");
    if (fac.empty() || !fac.first().boolv("network_sharing_enabled")) {
      out["status"] = "NOT_PARTICIPATING";
      out["message"] = "Your clinic does not take part in the ClinicFlow network. An administrator can join under a data-sharing "
                       "agreement (Settings > Clinic network).";
      out["snapshotId"] = snapshot(c, ctx, id, "NETWORK", out, {});
      return out;
    }
    if (cc.dxCode.empty()) {
      out["status"] = "INSUFFICIENT_DATA";
      out["message"] = "Record a coded (ICD-10) working diagnosis to compare with network cases.";
      out["snapshotId"] = snapshot(c, ctx, id, "NETWORK", out, {});
      return out;
    }
    std::string myKey = pseudonym("doctor", *ctx.doctorId);
    std::string ageBandV = cc.ageYears ? ageBand(*cc.ageYears) : "";
    std::vector<PatternCase> cases;
    json result;
    for (auto& t : kTiers) {
      if (t.severity && facts.severity.empty()) continue;
      if (t.age && ageBandV.empty()) continue;
      std::string sql = "SELECT generic_codes, regimen::text AS regimen, coalesce(outcome, '') AS outcome, doctor_key, facility_key, "
                        "  period_month::text AS m FROM network_case_records "
                        "WHERE withdrawn_at IS NULL AND dx_code = $1 AND doctor_key <> $2";
      Params p(cc.dxCode, myKey);
      int n = 2;
      if (t.severity) { sql += " AND severity = $" + std::to_string(++n); p.add(facts.severity); }
      if (t.age) { sql += " AND age_band = $" + std::to_string(++n); p.add(ageBandV); }
      sql += pregnancyClause(cc.pregnancy, 0) + " AND period_month >= date_trunc('month', now() - interval '36 months') LIMIT 5000";
      cases.clear();
      c.exec(sql, p).each([&](const Row& r) {
        PatternCase pc;
        pc.generics = vec(r, "generic_codes");
        pc.items = json::parse(r.str("regimen", "[]"));
        pc.outcome = r.str("outcome");
        pc.doctorKey = r.str("doctor_key");
        pc.facilityKey = r.str("facility_key");
        pc.period = r.str("m").substr(0, 7);
        cases.push_back(pc);
      });
      result = summarizeNetwork(cases);
      if (result["status"] == "OK") {
        result["comparison"] = {{"tier", t.name}, {"description", t.label}};
        break;
      }
    }
    if (result.is_null()) result = summarizeNetwork({});
    for (auto& [k, v] : result.items()) out[k] = v;
    out["diagnosis"] = {{"code", cc.dxCode}, {"display", cc.dxDisplay}};
    std::vector<std::string> generics;
    json suggestions = json::array();
    if (out["status"] == "OK") {
      for (auto& m : out["medications"]) {
        std::string g = m["generic"];
        generics.push_back(g);
        auto nm = c.exec("SELECT generic_name FROM medication_catalogue WHERE generic_code = $1 ORDER BY (brand_name IS NULL) DESC LIMIT 1",
                         Params(g));
        m["name"] = nm.empty() ? g : nm.first().str("generic_name");
        m["products"] = catalogueProducts(c, g);
        m["patientAlerts"] = annotateForPatient(c, cc, g);
        auto& reg = m["usualRegimen"];
        suggestions.push_back({{"generic", g}, {"route", reg.value("route", "")}, {"frequency", reg.value("frequency", "")},
                               {"durationDays", reg.contains("durationDays") ? reg["durationDays"] : json(nullptr)}});
      }
    }
    out["suggestions"] = suggestions;
    out["snapshotId"] = snapshot(c, ctx, id, "NETWORK", out, generics);
    m_audit->record(c, ctx, AuditEvent{"CDS_NETWORK_VIEW", "consultation", id, cc.patientId, "SUCCESS", {{"status", out["status"]}}});
    return out;
  });
}

// ---------------------------------------------------------------------------
// Finalize hooks
// ---------------------------------------------------------------------------
void DermService::beforeFinalize(Conn& c, const AuthContext& ctx, const std::string& id, const FinalizeOptions& opts) {
  auto cc = loadConsult(c, id);
  json alerts = safetyAlerts(c, cc);
  if (!requiresOverride(alerts)) return;
  json blocking = json::array();
  for (auto& a : alerts)
    if (a.value("requiresOverride", false)) blocking.push_back(a);
  if (!opts.safetyOverrideReason || opts.safetyOverrideReason->size() < 5) {
    throw ApiError::withData(409, "SAFETY_ACKNOWLEDGEMENT_REQUIRED",
                             "Safety alerts need to be reviewed. Change the prescription or record a reason to proceed.",
                             {{"alerts", alerts}});
  }
  c.exec("INSERT INTO safety_overrides (consultation_id, alerts, reason, overridden_by) VALUES ($1,$2::jsonb,$3,$4)",
         Params(id, blocking.dump(), *opts.safetyOverrideReason, ctx.userId));
  m_audit->record(c, ctx, AuditEvent{"SAFETY_OVERRIDE", "consultation", id, cc.patientId, "SUCCESS",
                                     {{"alerts", blocking.size()}, {"reason", *opts.safetyOverrideReason}}});
}

void DermService::afterFinalize(Conn& c, const AuthContext& ctx, const std::string& id) {
  auto cc = loadConsult(c, id);
  if (cc.specialty != "DERMATOLOGY") return;
  auto facts = factsFor(c, cc);

  // Final prescription (coded)
  json items = json::array();
  std::vector<std::string> generics;
  struct FinalItem {
    std::string generic, name, dose, frequency, route, source;
    std::optional<long long> days;
    bool matched = false;
  };
  std::vector<FinalItem> finals;
  c.exec("SELECT i.generic_code, i.medicine_name, i.dose, i.frequency, i.route, i.duration_days, i.source_kind "
         "FROM prescription_items i JOIN prescriptions rx ON rx.id = i.prescription_id WHERE rx.consultation_id = $1 ORDER BY i.sort_order",
         Params(id))
      .each([&](const Row& r) {
        FinalItem f{r.str("generic_code"), r.str("medicine_name"), r.str("dose"), r.str("frequency"), r.str("route"), r.str("source_kind")};
        if (!r.isNull("duration_days")) f.days = r.i64v("duration_days");
        finals.push_back(f);
        if (!f.generic.empty()) {
          json it = {{"generic", f.generic}, {"name", f.name}, {"route", f.route}, {"frequency", f.frequency}};
          if (f.days) it["durationDays"] = *f.days;
          items.push_back(it);
          if (std::find(generics.begin(), generics.end(), f.generic) == generics.end()) generics.push_back(f.generic);
        }
      });

  // Decisions: compare every suggestion the doctor was shown (latest snapshot per panel) with the final prescription.
  for (auto& src : kSources) {
    auto s = c.exec("SELECT id, payload::text AS payload FROM suggestion_snapshots WHERE consultation_id = $1 AND source = $2 "
                    "ORDER BY created_at DESC LIMIT 1", Params(id, src));
    if (s.empty()) continue;
    std::string snapId = s.first().str("id");
    json payload = json::parse(s.first().str("payload"));
    if (!payload.contains("suggestions")) continue;
    for (auto& sug : payload["suggestions"]) {
      std::string g = sug.value("generic", "");
      if (g.empty()) continue;
      std::string decision = "NOT_USED";
      json finalItem = nullptr;
      for (auto& f : finals) {
        if (f.generic != g) continue;
        f.matched = true;
        bool same = true;
        if (sug.contains("frequency") && sug["frequency"].is_string() && !sug["frequency"].get<std::string>().empty())
          same = same && sug["frequency"].get<std::string>() == f.frequency;
        if (sug.contains("dose") && sug["dose"].is_string()) same = same && sug["dose"].get<std::string>() == f.dose;
        if (sug.contains("durationDays") && sug["durationDays"].is_number())
          same = same && f.days && *f.days == sug["durationDays"].get<long long>();
        decision = same ? "ACCEPTED" : "MODIFIED";
        finalItem = {{"name", f.name}, {"dose", f.dose}, {"frequency", f.frequency}, {"route", f.route},
                     {"durationDays", f.days ? json(*f.days) : json(nullptr)}};
        break;
      }
      c.exec("INSERT INTO suggestion_decisions (consultation_id, snapshot_id, source, generic_code, decision, suggested, final_item, decided_by) "
             "VALUES ($1,$2,$3,$4,$5,$6::jsonb,$7::jsonb,$8)",
             Params(id, snapId, std::string(src), g, decision, sug.dump(), finalItem.is_null() ? std::optional<std::string>() : std::optional<std::string>(finalItem.dump()),
                    ctx.userId));
    }
  }
  for (auto& f : finals) {
    if (f.matched) continue;
    json finalItem = {{"name", f.name}, {"dose", f.dose}, {"frequency", f.frequency}, {"route", f.route},
                      {"durationDays", f.days ? json(*f.days) : json(nullptr)}};
    c.exec("INSERT INTO suggestion_decisions (consultation_id, source, generic_code, medicine_name, decision, final_item, decided_by) "
           "VALUES ($1,'DOCTOR',$2,$3,'DOCTOR_ADDED',$4::jsonb,$5)",
           Params(id, f.generic.empty() ? std::optional<std::string>() : std::optional<std::string>(f.generic), f.name, finalItem.dump(),
                  ctx.userId));
  }

  // Coded case for the doctor's own pattern analysis
  std::string ageBandV = cc.ageYears ? ageBand(*cc.ageYears) : "UNKNOWN";
  std::string durBand = durationBand(facts.durationDays);
  auto caseId = c.exec("INSERT INTO prescribing_cases (consultation_id, doctor_id, patient_id, finalized_at, dx_code, dx_display, "
                       "  problem_type, severity, age_band, sex, pregnancy_status, duration_band, recurring, body_sites, lesion_types, "
                       "  generic_codes, items) "
                       "SELECT $1, $2, $3, finalized_at, $4, $5, $6, $7, $8, $9, $10, $11, $12::boolean, $13::text[], $14::text[], "
                       "  $15::text[], $16::jsonb FROM consultations WHERE id = $1 RETURNING id",
                       Params(id, cc.doctorId, cc.patientId, cc.dxCode.empty() ? std::optional<std::string>() : std::optional<std::string>(cc.dxCode),
                              cc.dxDisplay.empty() ? std::optional<std::string>() : std::optional<std::string>(cc.dxDisplay),
                              facts.problemType.empty() ? std::optional<std::string>() : std::optional<std::string>(facts.problemType),
                              facts.severity.empty() ? std::optional<std::string>() : std::optional<std::string>(facts.severity),
                              ageBandV, cc.sex, cc.pregnancy.empty() ? std::optional<std::string>() : std::optional<std::string>(cc.pregnancy),
                              durBand.empty() ? std::optional<std::string>() : std::optional<std::string>(durBand),
                              facts.recurring ? std::optional<std::string>(*facts.recurring ? "true" : "false") : std::optional<std::string>(),
                              pgArr(std::vector<std::string>(facts.sites.begin(), facts.sites.end())),
                              pgArr(std::vector<std::string>(facts.lesions.begin(), facts.lesions.end())), pgArr(generics), items.dump()))
                    .first()
                    .str("id");

  // De-identified network record, only with clinic participation and without patient opt-out.
  auto gate = c.exec("SELECT f.network_sharing_enabled AS shared, p.network_opt_out AS opted_out FROM facilities f, patients p "
                     "WHERE f.id = $1 AND p.id = $2", Params(cc.facilityId, cc.patientId));
  bool share = !gate.empty() && gate.first().boolv("shared") && !gate.first().boolv("opted_out") && !cc.dxCode.empty() && !items.empty();
  if (share) {
    c.exec("INSERT INTO network_case_records (source_key, facility_key, doctor_key, period_month, specialty, dx_code, severity, age_band, "
           "  sex, pregnancy_status, duration_band, generic_codes, regimen) "
           "SELECT $1, $2, $3, date_trunc('month', finalized_at)::date, 'DERMATOLOGY', $4, $5, $6, $7, $8, $9, $10::text[], $11::jsonb "
           "FROM consultations WHERE id = $12",
           Params(pseudonym("case", caseId), pseudonym("facility", cc.facilityId), pseudonym("doctor", cc.doctorId), cc.dxCode,
                  facts.severity.empty() ? std::optional<std::string>() : std::optional<std::string>(facts.severity), ageBandV,
                  cc.sex == "MALE" || cc.sex == "FEMALE" ? std::optional<std::string>(cc.sex) : std::optional<std::string>(),
                  cc.pregnancy == "PREGNANT" ? std::optional<std::string>("PREGNANT") : std::optional<std::string>(),
                  durBand.empty() ? std::optional<std::string>() : std::optional<std::string>(durBand), pgArr(generics), items.dump(), id));
    c.exec("UPDATE prescribing_cases SET network_shared = true WHERE id = $1", Params(caseId));
  }
  m_audit->record(c, ctx, AuditEvent{"PRESCRIBING_CASE_CAPTURE", "consultation", id, cc.patientId, "SUCCESS",
                                     {{"networkShared", share}, {"medicines", generics.size()}}});
}

// ---------------------------------------------------------------------------
// Decisions & outcomes
// ---------------------------------------------------------------------------
json DermService::decisions(const AuthContext& ctx, const std::string& id) const {
  return m_db->withConn([&](Conn& c) {
    loadConsult(c, id);
    json out = {{"decisions", json::array()}, {"overrides", json::array()}, {"outcomes", json::array()}, {"panelsViewed", json::array()}};
    c.exec("SELECT d.*, u.full_name AS by_name FROM suggestion_decisions d JOIN users u ON u.id = d.decided_by "
           "WHERE d.consultation_id = $1 ORDER BY d.source, d.decision", Params(id))
        .each([&](const Row& r) {
          out["decisions"].push_back({{"source", r.str("source")}, {"generic", r.str("generic_code")},
                                      {"medicineName", r.str("medicine_name")}, {"decision", r.str("decision")},
                                      {"suggested", r.isNull("suggested") ? json(nullptr) : json::parse(r.str("suggested"))},
                                      {"finalItem", r.isNull("final_item") ? json(nullptr) : json::parse(r.str("final_item"))},
                                      {"decidedBy", r.str("by_name")}, {"decidedAt", *r.ts("decided_at")}});
        });
    c.exec("SELECT s.*, u.full_name AS by_name FROM safety_overrides s JOIN users u ON u.id = s.overridden_by "
           "WHERE s.consultation_id = $1 ORDER BY s.overridden_at", Params(id))
        .each([&](const Row& r) {
          out["overrides"].push_back({{"reason", r.str("reason")}, {"alerts", json::parse(r.str("alerts"))},
                                      {"by", r.str("by_name")}, {"at", *r.ts("overridden_at")}});
        });
    c.exec("SELECT o.*, u.full_name AS by_name FROM treatment_outcomes o JOIN users u ON u.id = o.recorded_by "
           "WHERE o.consultation_id = $1 ORDER BY o.recorded_at DESC", Params(id))
        .each([&](const Row& r) {
          out["outcomes"].push_back({{"outcome", r.str("outcome")},
                                     {"adverseEffects", r.isNull("adverse_effects") ? json(nullptr) : json(r.str("adverse_effects"))},
                                     {"notes", r.isNull("notes") ? json(nullptr) : json(r.str("notes"))},
                                     {"recordedBy", r.str("by_name")}, {"recordedAt", *r.ts("recorded_at")}});
        });
    c.exec("SELECT source, status, count(*) AS n, max(created_at) AS last FROM suggestion_snapshots WHERE consultation_id = $1 "
           "GROUP BY source, status ORDER BY source", Params(id))
        .each([&](const Row& r) {
          out["panelsViewed"].push_back({{"source", r.str("source")}, {"status", r.str("status")}, {"views", r.i64v("n")},
                                         {"lastViewedAt", *r.ts("last")}});
        });
    return out;
  });
}

json DermService::recordOutcome(const AuthContext& ctx, const std::string& id, const json& in) {
  auto outcome = jstr(in, "outcome");
  if (!outcome || !text::oneOf(*outcome, {"RESOLVED", "IMPROVED", "UNCHANGED", "WORSENED"}))
    throw ApiError::badRequest("Outcome is required", {{"outcome", "RESOLVED, IMPROVED, UNCHANGED or WORSENED"}});
  auto adverse = jstr(in, "adverseEffects");
  auto notes = jstr(in, "notes");
  if ((adverse && adverse->size() > 2000) || (notes && notes->size() > 2000))
    throw ApiError::badRequest("Text is limited to 2000 characters");
  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    if (cc.status != "FINALIZED") throw ApiError::conflict("NOT_FINALIZED", "Outcomes are recorded against finalized consultations");
    if (!ctx.doctorId) throw ApiError::forbidden("Only doctors can record treatment outcomes");
    c.exec("INSERT INTO treatment_outcomes (consultation_id, outcome, adverse_effects, notes, recorded_by) VALUES ($1,$2,$3,$4,$5)",
           Params(id, *outcome, adverse, notes, ctx.userId));
    auto cs = c.exec("UPDATE prescribing_cases SET outcome = $2, outcome_at = now() WHERE consultation_id = $1 RETURNING id, network_shared",
                     Params(id, *outcome));
    if (!cs.empty() && cs.first().boolv("network_shared")) {
      c.exec("UPDATE network_case_records SET outcome = $2 WHERE source_key = $1", Params(pseudonym("case", cs.first().str("id")), *outcome));
    }
    m_audit->record(c, ctx, AuditEvent{"TREATMENT_OUTCOME_RECORD", "consultation", id, cc.patientId, "SUCCESS",
                                       {{"outcome", *outcome}, {"adverseEffects", adverse.has_value()}}});
    return json{{"consultationId", id}, {"outcome", *outcome}, {"recorded", true}};
  });
}

// ---------------------------------------------------------------------------
// Analytics
// ---------------------------------------------------------------------------
json DermService::doctorAnalytics(const AuthContext& ctx) const {
  if (!ctx.doctorId) throw ApiError::forbidden("Analytics are available to users with a doctor profile");
  return m_db->withConn([&](Conn& c) {
    const std::string& d = *ctx.doctorId;
    json out;
    auto tz = "(SELECT timezone FROM clinic_settings WHERE id = 1)";
    auto today = c.exec(std::string("SELECT count(*) FILTER (WHERE status NOT IN ('CANCELLED')) AS total, "
                                    "count(*) FILTER (WHERE status IN ('SCHEDULED','CHECKED_IN')) AS pending, "
                                    "count(*) FILTER (WHERE status = 'IN_CONSULTATION') AS in_consult, "
                                    "count(*) FILTER (WHERE status = 'COMPLETED') AS completed, "
                                    "count(*) FILTER (WHERE status = 'NO_SHOW') AS no_show "
                                    "FROM appointments WHERE doctor_id = $1 AND (start_at AT TIME ZONE ") + tz +
                             ")::date = (now() AT TIME ZONE " + tz + ")::date",
                         Params(d))
                     .first();
    out["today"] = {{"appointments", today.i64v("total")}, {"pending", today.i64v("pending")},
                    {"inConsultation", today.i64v("in_consult")}, {"completed", today.i64v("completed")},
                    {"noShow", today.i64v("no_show")}};
    out["todayAppointments"] = json::array();
    c.exec(std::string("SELECT a.id, a.start_at, a.status, a.appointment_type, p.full_name, p.patient_number, "
                       "  (SELECT id FROM consultations WHERE appointment_id = a.id) AS consultation_id "
                       "FROM appointments a JOIN patients p ON p.id = a.patient_id WHERE a.doctor_id = $1 "
                       "AND (a.start_at AT TIME ZONE ") + tz + ")::date = (now() AT TIME ZONE " + tz + ")::date "
                       "AND a.status <> 'CANCELLED' ORDER BY a.start_at LIMIT 60",
           Params(d))
        .each([&](const Row& r) {
          out["todayAppointments"].push_back({{"appointmentId", r.str("id")}, {"startAt", *r.ts("start_at")}, {"status", r.str("status")},
                                              {"type", r.str("appointment_type")}, {"patientName", r.str("full_name")},
                                              {"patientNumber", r.str("patient_number")},
                                              {"consultationId", r.isNull("consultation_id") ? json(nullptr) : json(r.str("consultation_id"))}});
        });
    out["followUpsDue"] = json::array();
    c.exec(std::string("SELECT c.id, c.follow_up_date::text AS fud, p.full_name, p.patient_number, "
                       "  (SELECT outcome FROM treatment_outcomes o WHERE o.consultation_id = c.id ORDER BY recorded_at DESC LIMIT 1) AS outcome "
                       "FROM consultations c JOIN patients p ON p.id = c.patient_id WHERE c.doctor_id = $1 AND c.status = 'FINALIZED' "
                       "AND c.follow_up_date BETWEEN (now() AT TIME ZONE ") + tz + ")::date - 7 AND (now() AT TIME ZONE " + tz +
               ")::date + 14 ORDER BY c.follow_up_date LIMIT 50",
           Params(d))
        .each([&](const Row& r) {
          out["followUpsDue"].push_back({{"consultationId", r.str("id")}, {"followUpDate", r.str("fud")}, {"patientName", r.str("full_name")},
                                         {"patientNumber", r.str("patient_number")},
                                         {"outcomeRecorded", r.isNull("outcome") ? json(nullptr) : json(r.str("outcome"))}});
        });
    out["recentPrescriptions"] = json::array();
    c.exec("SELECT c.id, c.finalized_at, p.patient_number, "
           "  (SELECT display FROM consultation_diagnoses x WHERE x.consultation_id = c.id ORDER BY sort_order LIMIT 1) AS dx, "
           "  (SELECT count(*) FROM prescription_items i JOIN prescriptions rx ON rx.id = i.prescription_id WHERE rx.consultation_id = c.id) AS meds "
           "FROM consultations c JOIN patients p ON p.id = c.patient_id WHERE c.doctor_id = $1 AND c.status = 'FINALIZED' "
           "ORDER BY c.finalized_at DESC LIMIT 10",
           Params(d))
        .each([&](const Row& r) {
          out["recentPrescriptions"].push_back({{"consultationId", r.str("id")}, {"finalizedAt", *r.ts("finalized_at")},
                                                {"patientNumber", r.str("patient_number")}, {"diagnosis", r.str("dx")},
                                                {"medicines", r.i64v("meds")}});
        });
    out["topMedicines90d"] = json::array();
    c.exec("SELECT coalesce(m.generic_name, i.medicine_name) AS name, count(*) AS n FROM prescription_items i "
           "JOIN prescriptions rx ON rx.id = i.prescription_id LEFT JOIN medication_catalogue m ON m.id = i.medication_id "
           "WHERE rx.doctor_id = $1 AND rx.status = 'FINALIZED' AND rx.finalized_at > now() - interval '90 days' "
           "GROUP BY 1 ORDER BY n DESC, name LIMIT 10",
           Params(d))
        .each([&](const Row& r) { out["topMedicines90d"].push_back({{"name", r.str("name")}, {"count", r.i64v("n")}}); });
    out["topDiagnoses90d"] = json::array();
    c.exec("SELECT coalesce(x.code || ' ', '') || x.display AS dx, count(*) AS n FROM consultation_diagnoses x "
           "JOIN consultations c ON c.id = x.consultation_id WHERE c.doctor_id = $1 AND c.status = 'FINALIZED' "
           "AND c.finalized_at > now() - interval '90 days' AND x.sort_order = 0 GROUP BY 1 ORDER BY n DESC LIMIT 8",
           Params(d))
        .each([&](const Row& r) { out["topDiagnoses90d"].push_back({{"diagnosis", r.str("dx")}, {"count", r.i64v("n")}}); });
    out["monthly"] = json::array();
    c.exec(std::string("WITH m AS (SELECT generate_series(date_trunc('month', now() AT TIME ZONE ") + tz + ") - interval '5 months', "
                       "  date_trunc('month', now() AT TIME ZONE " + tz + "), interval '1 month')::date AS month) "
                       "SELECT to_char(m.month, 'YYYY-MM') AS month, "
                       "  (SELECT count(*) FROM consultations c WHERE c.doctor_id = $1 AND c.status = 'FINALIZED' "
                       "     AND date_trunc('month', c.finalized_at AT TIME ZONE " + tz + ")::date = m.month) AS consultations, "
                       "  (SELECT count(*) FROM prescribing_cases pc WHERE pc.doctor_id = $1 "
                       "     AND date_trunc('month', pc.finalized_at AT TIME ZONE " + tz + ")::date = m.month) AS derm_cases "
                       "FROM m ORDER BY m.month",
           Params(d))
        .each([&](const Row& r) {
          out["monthly"].push_back({{"month", r.str("month")}, {"consultations", r.i64v("consultations")}, {"dermCases", r.i64v("derm_cases")}});
        });
    auto oc = c.exec("SELECT count(*) AS cases, count(outcome) AS with_outcome, "
                     "count(*) FILTER (WHERE outcome IN ('RESOLVED','IMPROVED')) AS better, "
                     "count(*) FILTER (WHERE outcome = 'UNCHANGED') AS same, count(*) FILTER (WHERE outcome = 'WORSENED') AS worse "
                     "FROM prescribing_cases WHERE doctor_id = $1", Params(d)).first();
    out["outcomes"] = {{"cases", oc.i64v("cases")}, {"withOutcome", oc.i64v("with_outcome")}, {"improvedOrResolved", oc.i64v("better")},
                       {"unchanged", oc.i64v("same")}, {"worsened", oc.i64v("worse")}};
    out["suggestionUse90d"] = json::array();
    c.exec("SELECT d.source, d.decision, count(*) AS n FROM suggestion_decisions d JOIN consultations c ON c.id = d.consultation_id "
           "WHERE c.doctor_id = $1 AND d.decided_at > now() - interval '90 days' GROUP BY 1, 2 ORDER BY 1, 2",
           Params(d))
        .each([&](const Row& r) {
          out["suggestionUse90d"].push_back({{"source", r.str("source")}, {"decision", r.str("decision")}, {"count", r.i64v("n")}});
        });
    out["scope"] = "Your own consultations at this clinic only.";
    return out;
  });
}

json DermService::networkAnalytics(const AuthContext& ctx) const {
  return m_db->withConn([&](Conn& c) {
    json out = {{"diagnoses", json::array()}};
    auto fac = c.exec("SELECT network_sharing_enabled FROM facilities WHERE id = cf_current_facility()");
    if (fac.empty() || !fac.first().boolv("network_sharing_enabled")) {
      out["status"] = "NOT_PARTICIPATING";
      out["message"] = "Your clinic does not take part in the ClinicFlow network.";
      return out;
    }
    NetworkThresholds t;
    int suppressed = 0;
    c.exec("SELECT dx_code, count(*) AS n, count(DISTINCT doctor_key) AS doctors, count(DISTINCT facility_key) AS clinics, "
           "  max(cnt) AS max_from_one, min(period_month)::text AS p_from, max(period_month)::text AS p_to, count(outcome) AS outcomes "
           "FROM (SELECT *, count(*) OVER (PARTITION BY dx_code, facility_key) AS cnt FROM network_case_records "
           "      WHERE withdrawn_at IS NULL AND period_month >= date_trunc('month', now() - interval '36 months')) x "
           "GROUP BY dx_code ORDER BY n DESC")
        .each([&](const Row& r) {
          long long n = r.i64v("n");
          bool ok = n >= t.minCases && r.i64v("doctors") >= t.minDoctors && r.i64v("clinics") >= t.minFacilities &&
                    static_cast<double>(r.i64v("max_from_one")) / n <= t.maxFacilityShare;
          if (!ok) {
            ++suppressed;
            return;
          }
          auto name = c.exec("SELECT display FROM diagnosis_codes WHERE code = $1 LIMIT 1", Params(r.str("dx_code")));
          out["diagnoses"].push_back({{"code", r.str("dx_code")}, {"display", name.empty() ? r.str("dx_code") : name.first().str("display")},
                                      {"cases", n}, {"doctors", r.i64v("doctors")}, {"clinics", r.i64v("clinics")},
                                      {"casesWithOutcome", r.i64v("outcomes")},
                                      {"period", {{"from", r.str("p_from").substr(0, 7)}, {"to", r.str("p_to").substr(0, 7)}}}});
        });
    out["status"] = "OK";
    out["suppressedDiagnoses"] = suppressed;
    out["thresholds"] = {{"minCases", t.minCases}, {"minDoctors", t.minDoctors}, {"minClinics", t.minFacilities},
                         {"maxShareFromOneClinicPercent", static_cast<int>(t.maxFacilityShare * 100)}};
    out["note"] = "Aggregate, de-identified counts from participating clinics (last 36 months). Diagnoses below the "
                  "disclosure thresholds are not listed.";
    return out;
  });
}

// ---------------------------------------------------------------------------
// Clinical content review
// ---------------------------------------------------------------------------
json DermService::contentList(const AuthContext& ctx, const std::string& status) const {
  std::string st = status.empty() ? "ALL" : status;
  if (!text::oneOf(st, {"ALL", "UNREVIEWED", "APPROVED", "RETIRED"})) throw ApiError::badRequest("Unknown status filter");
  return m_db->withConn([&](Conn& c) {
    json out = {{"conditions", json::array()}, {"redFlags", json::array()}, {"medications", json::array()}, {"interactions", json::array()}};
    Params p(st);
    c.exec("SELECT k.*, k.refs::text AS refs_t, u.full_name AS reviewer FROM cds_conditions k LEFT JOIN users u ON u.id = k.reviewed_by "
           "WHERE $1 = 'ALL' OR k.review_status = $1 ORDER BY k.name", p)
        .each([&](const Row& r) {
          json k = {{"id", r.str("id")}, {"code", r.str("code")}, {"name", r.str("name")}, {"icd10", r.str("icd10_code")},
                    {"summary", r.str("summary")}, {"references", json::parse(r.str("refs_t", "[]"))},
                    {"contentSource", r.str("content_source")}, {"reviewStatus", r.str("review_status")},
                    {"reviewedBy", r.isNull("reviewer") ? json(nullptr) : json(r.str("reviewer"))},
                    {"reviewedAt", r.isNull("reviewed_at") ? json(nullptr) : json(*r.ts("reviewed_at"))},
                    {"reviewNotes", r.isNull("review_notes") ? json(nullptr) : json(r.str("review_notes"))},
                    {"features", json::array()}, {"content", json::array()}};
          c.exec("SELECT feature, weight FROM cds_condition_features WHERE condition_id = $1 ORDER BY weight DESC, feature",
                 Params(r.str("id")))
              .each([&](const Row& f) {
                k["features"].push_back({{"token", f.str("feature")}, {"label", tokenLabel(f.str("feature"))}, {"weight", f.i64v("weight")}});
              });
          c.exec("SELECT kind, text, generic_codes, line, severities FROM cds_condition_content WHERE condition_id = $1 ORDER BY sort_order",
                 Params(r.str("id")))
              .each([&](const Row& x) {
                k["content"].push_back({{"kind", x.str("kind")}, {"text", x.str("text")}, {"generics", vec(x, "generic_codes")},
                                        {"line", x.str("line")}, {"severities", vec(x, "severities")}});
              });
          out["conditions"].push_back(k);
        });
    c.exec("SELECT f.*, f.criteria::text AS crit, u.full_name AS reviewer FROM cds_red_flags f LEFT JOIN users u ON u.id = f.reviewed_by "
           "WHERE $1 = 'ALL' OR f.review_status = $1 ORDER BY f.title", p)
        .each([&](const Row& r) {
          json crit = json::parse(r.str("crit"));
          json labels = json::object();
          for (auto key : {"all", "any"})
            if (crit.contains(key)) {
              labels[key] = json::array();
              for (auto& t : crit[key]) labels[key].push_back(tokenLabel(t.get<std::string>()));
            }
          out["redFlags"].push_back({{"id", r.str("id")}, {"code", r.str("code")}, {"title", r.str("title")}, {"criteria", labels},
                                     {"guidance", r.str("guidance")}, {"reference", r.isNull("reference") ? json(nullptr) : json(r.str("reference"))},
                                     {"contentSource", r.str("content_source")}, {"reviewStatus", r.str("review_status")},
                                     {"reviewedBy", r.isNull("reviewer") ? json(nullptr) : json(r.str("reviewer"))},
                                     {"reviewNotes", r.isNull("review_notes") ? json(nullptr) : json(r.str("review_notes"))}});
        });
    c.exec("SELECT m.*, m.contraindication_rules::text AS rules, u.full_name AS reviewer FROM medication_catalogue m "
           "LEFT JOIN users u ON u.id = m.reviewed_by WHERE m.generic_code IS NOT NULL AND ($1 = 'ALL' OR m.review_status = $1) "
           "ORDER BY m.category NULLS LAST, m.generic_name, m.brand_name NULLS FIRST", p)
        .each([&](const Row& r) {
          auto s = [&](const char* col) { return r.isNull(col) ? json(nullptr) : json(r.str(col)); };
          out["medications"].push_back({{"id", r.str("id")}, {"genericCode", r.str("generic_code")}, {"genericName", r.str("generic_name")},
                                        {"brandName", s("brand_name")}, {"strength", r.str("strength")}, {"dosageForm", r.str("dosage_form")},
                                        {"route", r.str("default_route")}, {"category", s("category")}, {"drugSchedule", s("drug_schedule")},
                                        {"commonDosage", s("common_dosage")}, {"warnings", s("warnings")},
                                        {"contraindicationRules", json::parse(r.str("rules", "[]"))},
                                        {"contraindicationsText", s("contraindications_text")}, {"interactionsText", s("interactions_text")},
                                        {"highRisk", r.boolv("high_risk")}, {"awareCategory", s("aware_category")},
                                        {"restrictedAntimicrobial", r.boolv("restricted_antimicrobial")},
                                        {"defaultDose", s("default_dose")}, {"defaultFrequency", s("default_frequency")},
                                        {"defaultDurationDays", r.isNull("default_duration_days") ? json(nullptr) : json(r.i64v("default_duration_days"))},
                                        {"reference", s("reference")}, {"contentSource", r.str("content_source")},
                                        {"reviewStatus", r.str("review_status")}, {"reviewedBy", s("reviewer")}, {"reviewNotes", s("review_notes")},
                                        {"isActive", r.boolv("is_active")}});
        });
    c.exec("SELECT i.*, u.full_name AS reviewer FROM medication_interactions i LEFT JOIN users u ON u.id = i.reviewed_by "
           "WHERE $1 = 'ALL' OR i.review_status = $1 ORDER BY i.severity, i.generic_a", p)
        .each([&](const Row& r) {
          out["interactions"].push_back({{"id", r.str("id")}, {"genericA", r.str("generic_a")}, {"genericB", r.str("generic_b")},
                                         {"severity", r.str("severity")}, {"description", r.str("description")}, {"reference", r.str("reference")},
                                         {"reviewStatus", r.str("review_status")},
                                         {"reviewedBy", r.isNull("reviewer") ? json(nullptr) : json(r.str("reviewer"))},
                                         {"reviewNotes", r.isNull("review_notes") ? json(nullptr) : json(r.str("review_notes"))}});
        });
    auto counts = c.exec("SELECT "
                         "(SELECT count(*) FROM cds_conditions WHERE review_status='UNREVIEWED') + "
                         "(SELECT count(*) FROM cds_red_flags WHERE review_status='UNREVIEWED') + "
                         "(SELECT count(*) FROM medication_catalogue WHERE generic_code IS NOT NULL AND review_status='UNREVIEWED') + "
                         "(SELECT count(*) FROM medication_interactions WHERE review_status='UNREVIEWED') AS pending")
                      .first();
    out["pendingReview"] = counts.i64v("pending");
    out["notice"] = "Starter content (source STARTER_DRAFT) was drafted by an AI assistant from the cited guidelines and has not "
                    "been verified. Check each item and citation against the current source before approving. Only APPROVED "
                    "content is used for decision support.";
    return out;
  });
}

json DermService::reviewContent(const AuthContext& ctx, const std::string& type, const std::string& id, const json& in) {
  static const std::map<std::string, std::string> tables = {{"condition", "cds_conditions"}, {"red-flag", "cds_red_flags"},
                                                            {"medication", "medication_catalogue"},
                                                            {"interaction", "medication_interactions"}};
  auto t = tables.find(type);
  if (t == tables.end()) throw ApiError::notFound("Content type");
  if (!text::isUuid(id)) throw ApiError::notFound("Content item");
  auto action = jstr(in, "action").value_or("");
  if (!text::oneOf(action, {"APPROVE", "RETIRE", "REOPEN"})) throw ApiError::badRequest("Action must be APPROVE, RETIRE or REOPEN", {{"action", "Invalid"}});
  auto notes = jstr(in, "notes");
  if (action == "APPROVE" && (!notes || notes->size() < 10))
    throw ApiError::badRequest("Record what the item was checked against (at least 10 characters)", {{"notes", "Required for approval"}});
  std::string newStatus = action == "APPROVE" ? "APPROVED" : action == "RETIRE" ? "RETIRED" : "UNREVIEWED";
  return m_db->tx([&](Conn& c) {
    if (t->second == "medication_catalogue" && action == "APPROVE") {
      auto r = c.exec("SELECT reference IS NOT NULL AS has_ref FROM medication_catalogue WHERE id = $1", Params(id));
      if (r.empty()) throw ApiError::notFound("Content item");
      if (!r.first().boolv("has_ref")) throw ApiError::badRequest("Add a reference before approving", {{"reference", "Required"}});
    }
    auto r = c.exec("UPDATE " + t->second + " SET review_status = $2, reviewed_by = $3, reviewed_at = now(), review_notes = $4 "
                    "WHERE id = $1 RETURNING review_status",
                    Params(id, newStatus, ctx.userId, notes));
    if (r.empty()) throw ApiError::notFound("Content item");
    if (type == "condition" && action == "REOPEN")
      c.exec("UPDATE cds_conditions SET content_version = content_version + 1 WHERE id = $1", Params(id));
    m_audit->record(c, ctx, AuditEvent{"CDS_CONTENT_REVIEW", type, id, std::nullopt, "SUCCESS",
                                       {{"action", action}, {"status", newStatus}, {"notes", notes.value_or("")}}});
    return json{{"id", id}, {"type", type}, {"reviewStatus", newStatus}};
  });
}

json DermService::updateMedicationClinical(const AuthContext& ctx, const std::string& id, const json& in) {
  if (!text::isUuid(id)) throw ApiError::notFound("Medication");
  Validator v;
  auto aware = jstr(in, "awareCategory");
  if (aware) v.check(text::oneOf(*aware, {"ACCESS", "WATCH", "RESERVE"}), "awareCategory", "ACCESS, WATCH or RESERVE");
  json rules = in.contains("contraindicationRules") && !in["contraindicationRules"].is_null() ? in["contraindicationRules"] : json::array();
  if (!rules.is_array()) v.fail("contraindicationRules", "Must be a list");
  for (auto& r : rules) {
    if (!r.is_object() || !r.contains("tag") || !r["tag"].is_string() || !r.contains("level") || !r["level"].is_string()) {
      v.fail("contraindicationRules", "Each rule needs a tag and a level");
      break;
    }
    std::string tag = r["tag"], level = r["level"];
    bool okTag = tag == "PREGNANCY" || tag == "BREASTFEEDING" || tag.rfind("AGE_UNDER_", 0) == 0;
    if (!okTag || !text::oneOf(level, {"CONTRAINDICATED", "CAUTION"})) v.fail("contraindicationRules", "Unknown tag or level");
  }
  auto days = jnum(in, "defaultDurationDays");
  if (days) v.check(*days >= 1 && *days <= 365, "defaultDurationDays", "1 to 365");
  std::map<std::string, std::optional<std::string>> s;
  for (auto k : {"category", "commonDosage", "warnings", "contraindicationsText", "interactionsText", "reference", "defaultDose",
                 "defaultFrequency", "genericCode"}) {
    s[k] = jstr(in, k);
    if (s[k] && s[k]->size() > 2000) v.fail(k, "Too long");
  }
  if (s["genericCode"]) v.check(std::regex_match(*s["genericCode"], std::regex("^[A-Z0-9_]{2,60}$")), "genericCode", "Uppercase code");
  v.throwIfAny();
  return m_db->tx([&](Conn& c) {
    // Any change to clinical information sends the item back for review.
    auto r = c.exec("UPDATE medication_catalogue SET category = $2, common_dosage = $3, warnings = $4, contraindications_text = $5, "
                    "interactions_text = $6, reference = $7, default_dose = $8, default_frequency = $9, default_duration_days = $10::int, "
                    "contraindication_rules = $11::jsonb, high_risk = coalesce($12::boolean, high_risk), aware_category = $13, "
                    "restricted_antimicrobial = coalesce($14::boolean, restricted_antimicrobial), "
                    "generic_code = coalesce(generic_code, $15), "
                    "ingredient_codes = CASE WHEN cardinality(ingredient_codes) = 0 AND $15::text IS NOT NULL THEN ARRAY[$15::text] ELSE ingredient_codes END, "
                    "review_status = 'UNREVIEWED', reviewed_by = NULL, reviewed_at = NULL, review_notes = NULL WHERE id = $1 RETURNING id",
                    Params(id, s["category"], s["commonDosage"], s["warnings"], s["contraindicationsText"], s["interactionsText"],
                           s["reference"], s["defaultDose"], s["defaultFrequency"],
                           days ? std::optional<std::string>(std::to_string(static_cast<int>(*days))) : std::optional<std::string>(),
                           rules.dump(),
                           jbool(in, "highRisk") ? std::optional<std::string>(*jbool(in, "highRisk") ? "true" : "false") : std::optional<std::string>(),
                           aware,
                           jbool(in, "restrictedAntimicrobial") ? std::optional<std::string>(*jbool(in, "restrictedAntimicrobial") ? "true" : "false")
                                                                : std::optional<std::string>(),
                           s["genericCode"]));
    if (r.empty()) throw ApiError::notFound("Medication");
    m_audit->record(c, ctx, AuditEvent{"CATALOGUE_MEDICATION_CLINICAL_UPDATE", "medication", id, std::nullopt, "SUCCESS",
                                       {{"reviewStatus", "UNREVIEWED"}}});
    return json{{"id", id}, {"reviewStatus", "UNREVIEWED"}};
  });
}

// ---------------------------------------------------------------------------
// Network participation
// ---------------------------------------------------------------------------
json DermService::setPatientNetworkOptOut(const AuthContext& ctx, const std::string& patientId, const json& in) {
  if (!text::isUuid(patientId)) throw ApiError::notFound("Patient");
  auto optOut = jbool(in, "optOut");
  if (!optOut) throw ApiError::badRequest("optOut (true/false) is required", {{"optOut", "Required"}});
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("UPDATE patients SET network_opt_out = $2::boolean, network_opt_out_at = CASE WHEN $2::boolean THEN now() END "
                    "WHERE id = $1 RETURNING id", Params(patientId, std::string(*optOut ? "true" : "false")));
    if (r.empty()) throw ApiError::notFound("Patient");
    int withdrawn = 0;
    if (*optOut) {
      // Withdraw this patient's previously contributed de-identified records.
      c.exec("SELECT id FROM prescribing_cases WHERE patient_id = $1 AND network_shared", Params(patientId)).each([&](const Row& x) {
        c.exec("UPDATE network_case_records SET withdrawn_at = now() WHERE source_key = $1 AND withdrawn_at IS NULL",
               Params(pseudonym("case", x.str("id"))));
        ++withdrawn;
      });
      c.exec("UPDATE prescribing_cases SET network_shared = false WHERE patient_id = $1", Params(patientId));
    }
    m_audit->record(c, ctx, AuditEvent{"PATIENT_NETWORK_OPT_OUT", "patient", patientId, patientId, "SUCCESS",
                                       {{"optOut", *optOut}, {"recordsWithdrawn", withdrawn}}});
    return json{{"patientId", patientId}, {"networkOptOut", *optOut}, {"recordsWithdrawn", withdrawn}};
  });
}

void DermService::withdrawFacilityFromNetwork(Conn& c, const std::string& facilityId) const {
  c.exec("UPDATE network_case_records SET withdrawn_at = now() WHERE facility_key = $1 AND withdrawn_at IS NULL",
         Params(pseudonym("facility", facilityId)));
}

}  // namespace cf
