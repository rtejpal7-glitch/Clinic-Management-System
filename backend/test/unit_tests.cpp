// Unit tests for pure backend logic (no database or network).
// Run: ctest --test-dir build  (or ./build/clinicflow-unit-tests)

#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "abdm/AbdmService.hpp"
#include "abdm/MockAbdmGateway.hpp"
#include "fhir/FhirMapper.hpp"
#include "security/AuthService.hpp"
#include "security/Crypto.hpp"
#include "service/AppointmentStateMachine.hpp"
#include "service/BillingMath.hpp"
#include "service/DuplicateScoring.hpp"
#include "service/NotificationService.hpp"
#include "service/PatientService.hpp"
#include "derm/CdsEngine.hpp"
#include "derm/DermVocabulary.hpp"
#include "derm/MedicationSafety.hpp"
#include "derm/PatternStats.hpp"
#include "derm/ClinicalTextExtractor.hpp"
#include "hwr/HandwritingRecognizer.hpp"
#include "hwr/HttpsClient.hpp"

namespace {
int g_failed = 0, g_passed = 0;
std::vector<std::pair<std::string, std::function<void()>>>& registry() {
  static std::vector<std::pair<std::string, std::function<void()>>> r;
  return r;
}
struct Reg {
  Reg(const std::string& n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
#define TEST(name) \
  static void name(); \
  static Reg reg_##name(#name, name); \
  static void name()
#define CHECK(cond) \
  do { if (!(cond)) throw Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + "  CHECK(" #cond ")"); } while (0)
#define CHECK_EQ(a, b) \
  do { auto _a = (a); auto _b = (b); if (!(_a == _b)) throw Failure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
       "  CHECK_EQ(" #a ", " #b ")"); } while (0)
#define CHECK_THROWS(stmt) \
  do { bool _t = false; try { stmt; } catch (...) { _t = true; } if (!_t) throw Failure(std::string(__FILE__) + ":" + \
       std::to_string(__LINE__) + "  expected exception: " #stmt); } while (0)
}  // namespace

using namespace cf;

// ---------------- Billing math ----------------
TEST(billing_simple_totals) {
  auto t = computeBill({{1, 50000, 0}, {2, 10000, 0}}, 0);
  CHECK_EQ(t.subtotal, 70000);
  CHECK_EQ(t.tax, 0);
  CHECK_EQ(t.total, 70000);
}
TEST(billing_discount_allocation_sums_exactly) {
  // 100 paise discount over 3 equal lines cannot split evenly: last line absorbs remainder.
  auto t = computeBill({{1, 1000, 0}, {1, 1000, 0}, {1, 1000, 0}}, 100);
  CHECK_EQ(t.lines[0].discount + t.lines[1].discount + t.lines[2].discount, 100);
  CHECK_EQ(t.lines[0].discount, 33);
  CHECK_EQ(t.lines[2].discount, 34);
  CHECK_EQ(t.total, 2900);
}
TEST(billing_tax_after_discount_round_half_up) {
  // 18% GST on (20000 - 5000) = 2700
  auto t = computeBill({{1, 20000, 1800}}, 5000);
  CHECK_EQ(t.tax, 2700);
  CHECK_EQ(t.total, 17700);
  // rounding: 18% of 333 = 59.94 -> 60
  auto r = computeBill({{1, 333, 1800}}, 0);
  CHECK_EQ(r.tax, 60);
}
TEST(billing_mixed_tax_lines) {
  auto t = computeBill({{1, 50000, 0}, {1, 20000, 1800}}, 7000);
  // discount split 5000 / 2000; tax on 18000 at 18% = 3240
  CHECK_EQ(t.lines[0].discount, 5000);
  CHECK_EQ(t.lines[1].discount, 2000);
  CHECK_EQ(t.tax, 3240);
  CHECK_EQ(t.total, 70000 - 7000 + 3240);
}
TEST(billing_rejects_invalid_input) {
  CHECK_THROWS(computeBill({}, 0));
  CHECK_THROWS(computeBill({{1, 100, 0}}, 101));
  CHECK_THROWS(computeBill({{0, 100, 0}}, 0));
  CHECK_THROWS(computeBill({{1, 100, 0}}, -1));
}
TEST(billing_payment_status) {
  CHECK_EQ(std::string(paymentStatus(1000, 0, 0)), "PENDING");
  CHECK_EQ(std::string(paymentStatus(1000, 400, 0)), "PARTIALLY_PAID");
  CHECK_EQ(std::string(paymentStatus(1000, 1000, 0)), "PAID");
  CHECK_EQ(std::string(paymentStatus(1000, 1000, 1000)), "REFUNDED");
  CHECK_EQ(std::string(paymentStatus(1000, 1000, 200)), "PAID");
}

// ---------------- Duplicate scoring ----------------
TEST(duplicate_exact_match_scores_high) {
  DuplicateRules r;
  DuplicateProbe p{"ramesh kumar", "1980-05-01", "9876543210", "MALE"};
  auto s = scoreDuplicate(r, p, {"1980-05-01", "9876543210", "MALE", 1.0});
  CHECK_EQ(s.score, 100);
  CHECK(s.isPossibleDuplicate);
  CHECK_EQ(s.matchedOn.size(), (size_t)3);
}
TEST(duplicate_same_mobile_different_person_below_threshold) {
  // Family members often share a mobile number: mobile alone (40) < threshold (60)
  DuplicateRules r;
  DuplicateProbe p{"anita sharma", "2015-01-01", "9876543210", "FEMALE"};
  auto s = scoreDuplicate(r, p, {"1982-03-03", "9876543210", "MALE", 0.2});
  CHECK_EQ(s.score, 40);
  CHECK(!s.isPossibleDuplicate);
}
TEST(duplicate_name_and_dob_flags) {
  DuplicateRules r;
  DuplicateProbe p{"sunita devi", "1975-07-07", "9000000001", "FEMALE"};
  auto s = scoreDuplicate(r, p, {"1975-07-07", "9111111111", "FEMALE", 0.9});
  CHECK_EQ(s.score, 60);
  CHECK(s.isPossibleDuplicate);
}
TEST(duplicate_rules_are_configurable) {
  DuplicateRules r;
  r.threshold = 30;
  DuplicateProbe p{"x y", "2000-01-01", "9876500000", "MALE"};
  auto s = scoreDuplicate(r, p, {"1999-01-01", "9876500000", "MALE", 0.0});
  CHECK(s.isPossibleDuplicate);  // mobile alone now enough
}

// ---------------- Appointment state machine ----------------
TEST(appointment_transitions) {
  CHECK(canTransition("SCHEDULED", "CHECKED_IN"));
  CHECK(canTransition("CHECKED_IN", "IN_CONSULTATION"));
  CHECK(canTransition("IN_CONSULTATION", "COMPLETED"));
  CHECK(canTransition("SCHEDULED", "NO_SHOW"));
  CHECK(!canTransition("COMPLETED", "CANCELLED"));
  CHECK(!canTransition("CANCELLED", "SCHEDULED"));
  CHECK(!canTransition("SCHEDULED", "COMPLETED"));
  CHECK(!canTransition("IN_CONSULTATION", "CANCELLED"));
  CHECK(isTerminal("NO_SHOW"));
}

// ---------------- Crypto ----------------
TEST(crypto_field_encryption_roundtrip_and_tamper) {
  std::string k1 = crypto::base64Encode(std::string(32, 'a'));
  std::string k2 = crypto::base64Encode(std::string(32, 'b'));
  crypto::FieldCipher c(k1, k2);
  auto ct = c.encrypt("12345678901234");
  CHECK(ct.rfind("v1:", 0) == 0);
  CHECK(ct.find("12345678901234") == std::string::npos);
  CHECK_EQ(c.decrypt(ct), std::string("12345678901234"));
  CHECK(c.encrypt("same") != c.encrypt("same"));  // random nonce
  std::string tampered = ct;
  tampered[tampered.size() - 3] = tampered[tampered.size() - 3] == 'A' ? 'B' : 'A';
  CHECK_THROWS(c.decrypt(tampered));
  CHECK_EQ(c.blindIndex("x"), c.blindIndex("x"));
  CHECK(c.blindIndex("x") != c.blindIndex("y"));
}
TEST(crypto_rejects_bad_keys) {
  CHECK_THROWS(crypto::FieldCipher(crypto::base64Encode("short"), crypto::base64Encode(std::string(32, 'b'))));
  std::string same = crypto::base64Encode(std::string(32, 'a'));
  CHECK_THROWS(crypto::FieldCipher(same, same));
}
TEST(password_hash_verify) {
  auto h = crypto::password::hash("Correct-Horse-42");
  CHECK(h.rfind("$argon2id$", 0) == 0);
  CHECK(crypto::password::verify(h, "Correct-Horse-42"));
  CHECK(!crypto::password::verify(h, "correct-horse-42"));
  CHECK(!crypto::password::verify("plaintext", "plaintext"));
  CHECK(crypto::password::hash("Correct-Horse-42") != h);  // salted
}
TEST(password_policy) {
  CHECK_THROWS(AuthService::validatePasswordPolicy("short1", "bob"));
  CHECK_THROWS(AuthService::validatePasswordPolicy("onlyletterslong", "bob"));
  CHECK_THROWS(AuthService::validatePasswordPolicy("reception1234", "reception"));
  AuthService::validatePasswordPolicy("Clinic-Flow-2026", "admin");
}
TEST(sha256_known_vector) {
  CHECK_EQ(crypto::sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

// ---------------- ABHA helpers ----------------
TEST(abha_normalisation_and_masking) {
  CHECK_EQ(PatientService::normalizeAbhaNumber("91-1234-5678-9012"), std::string("91123456789012"));
  CHECK_EQ(AbdmService::maskAbhaNumber("91123456789012"), std::string("XX-XXXX-XXXX-9012"));
  CHECK(AbdmService::isValidAbhaAddress("ramesh.k@abdm"));
  CHECK(AbdmService::isValidAbhaAddress("test_user@sbx"));
  CHECK(!AbdmService::isValidAbhaAddress("no-at-sign"));
  CHECK(!AbdmService::isValidAbhaAddress("UPPER@abdm"));
  auto m = AbdmService::maskAbhaAddress("ramesh.k@abdm");
  CHECK(m.rfind("ra", 0) == 0);
  CHECK(m.find("@abdm") != std::string::npos);
  CHECK(m.find("mesh") == std::string::npos);
  CHECK_EQ(NotificationService::maskPhone("9876543210"), std::string("xxxxxx3210"));
}

// ---------------- Mock gateway ----------------
TEST(mock_gateway_behaviour) {
  MockAbdmGateway g;
  CHECK(g.isMock());
  auto init = g.initAbhaVerification("r1", "ABHA_NUMBER", "91123456789012", "MOBILE_OTP");
  CHECK(init.ok);
  CHECK(init.gatewayTxnId.rfind("MOCK-", 0) == 0);
  CHECK(g.confirmAbhaVerification("r2", init.gatewayTxnId, "123456").ok);
  auto bad = g.confirmAbhaVerification("r3", init.gatewayTxnId, "111111");
  CHECK(!bad.ok);
  CHECK(!bad.retryable);
  auto to = g.confirmAbhaVerification("r4", init.gatewayTxnId, "999999");
  CHECK(!to.ok && to.retryable);
  g.injectFailures("HI_SHARE", 1, true);
  auto f = g.pushHealthInformation("r5", "CA", "CC", nlohmann::json::object());
  CHECK(!f.ok && f.retryable);
  CHECK(g.pushHealthInformation("r6", "CA", "CC", nlohmann::json::object()).ok);
}

// ---------------- FHIR validation ----------------
TEST(fhir_validator_detects_problems) {
  nlohmann::json comp = {{"resourceType", "Composition"}, {"id", "c1"}, {"status", "final"},
                         {"type", {{"coding", {{{"system", "http://snomed.info/sct"}, {"code", "371530004"}}}}}},
                         {"subject", {{"reference", "urn:uuid:p1"}}}, {"date", "2026-01-01T00:00:00Z"},
                         {"author", {{{"reference", "urn:uuid:d1"}}}}, {"title", "Consultation Report"}};
  nlohmann::json pat = {{"resourceType", "Patient"}, {"id", "p1"}, {"name", {{{"text", "A"}}}}, {"gender", "female"},
                        {"identifier", {{{"system", "urn:x"}, {"value", "1"}}}}, {"birthDate", "1990-01-01"}};
  nlohmann::json doc = {{"resourceType", "Practitioner"}, {"id", "d1"}, {"name", {{{"text", "Dr B"}}}}};
  nlohmann::json bundle = {{"resourceType", "Bundle"}, {"id", "b1"}, {"type", "document"}, {"timestamp", "2026-01-01T00:00:00Z"},
                           {"identifier", {{"system", "urn:x"}, {"value", "b1"}}},
                           {"entry", {{{"fullUrl", "urn:uuid:c1"}, {"resource", comp}},
                                      {{"fullUrl", "urn:uuid:p1"}, {"resource", pat}},
                                      {{"fullUrl", "urn:uuid:d1"}, {"resource", doc}}}}};
  CHECK(FhirMapper::validateBundle(bundle).empty());
  auto broken = bundle;
  broken["entry"][0]["resource"]["subject"]["reference"] = "urn:uuid:missing";
  broken["entry"][1]["resource"]["gender"] = "M";
  broken["entry"][1]["resource"].erase("identifier");
  auto issues = FhirMapper::validateBundle(broken);
  CHECK(issues.size() >= 3);
  auto wrongOrder = bundle;
  std::swap(wrongOrder["entry"][0], wrongOrder["entry"][1]);
  CHECK(!FhirMapper::validateBundle(wrongOrder).empty());
}

// ---------------- Dermatology decision support ----------------
using cf::derm::DermFacts;
using nlohmann::json;

static std::vector<cf::derm::CdsCondition> testConditions() {
  cf::derm::CdsCondition urt{"URTICARIA", "Urticaria", "L50.9", "wheals"};
  urt.features = {{"LESION:WHEAL", 5}, {"PROBLEM:HIVES_SWELLING", 3}, {"SYMPTOM:ITCHING", 2}};
  urt.content = {{"QUESTION", "Do wheals fade within 24 h?", {}, "", {}, "ref"},
                 {"TREATMENT", "Second-generation antihistamine", {"FEXOFENADINE"}, "FIRST", {}, "ref"},
                 {"TREATMENT", "Short steroid course for severe flares", {"PREDNISOLONE"}, "ADJUNCT", {"SEVERE"}, "ref"}};
  cf::derm::CdsCondition tinea{"TINEA_CORPORIS", "Tinea corporis", "B35.4", "ring"};
  tinea.features = {{"CONFIG:ANNULAR", 4}, {"LESION:SCALE", 2}, {"SYMPTOM:ITCHING", 2}};
  return {urt, tinea};
}

TEST(cds_requires_minimum_information) {
  DermFacts f;
  f.lesions = {"WHEAL"};
  auto r = cf::derm::evaluate(f, testConditions(), {}, 0);
  CHECK_EQ(r["status"].get<std::string>(), std::string("INSUFFICIENT_DATA"));
  CHECK(r["conditions"].empty());
  CHECK(r["missing"].size() == 2);  // problem type and duration
}

TEST(cds_never_uses_unapproved_content) {
  DermFacts f;
  f.problemType = "HIVES_SWELLING"; f.lesions = {"WHEAL"}; f.durationDays = 2;
  auto r = cf::derm::evaluate(f, {}, {}, 23);
  CHECK_EQ(r["status"].get<std::string>(), std::string("NO_APPROVED_CONTENT"));
  CHECK(r["conditions"].empty() && r["treatments"].empty());
  CHECK(r["message"].get<std::string>().find("23") != std::string::npos);
}

TEST(cds_ranks_with_reasons_and_filters_by_severity) {
  DermFacts f;
  f.problemType = "HIVES_SWELLING"; f.lesions = {"WHEAL"}; f.symptoms = {"ITCHING"}; f.durationDays = 3; f.severity = "MILD";
  auto r = cf::derm::evaluate(f, testConditions(), {}, 0);
  CHECK_EQ(r["status"].get<std::string>(), std::string("OK"));
  CHECK_EQ(r["conditions"][0]["code"].get<std::string>(), std::string("URTICARIA"));
  CHECK_EQ(r["conditions"][0]["strength"].get<std::string>(), std::string("Strong match"));
  CHECK(r["conditions"][0]["matched"].size() == 3);
  // Tinea shares only itching (2/8 = 0.25) - shown as weak at most, never above urticaria
  for (auto& t : r["treatments"]) CHECK(t["text"].get<std::string>().find("steroid") == std::string::npos);  // SEVERE-only filtered
  CHECK(r["questions"].size() >= 1);
}

TEST(cds_red_flags_fire_even_with_insufficient_data) {
  DermFacts f;
  f.sites = {"MUCOSA"}; f.lesions = {"EROSION"};
  cf::derm::CdsRedFlag sjs{"SJS", "Possible SJS/TEN", "Urgent", "ref", json{{"all", {"SITE:MUCOSA"}}, {"any", {"LESION:BULLA", "LESION:EROSION"}}}};
  cf::derm::CdsRedFlag ulcer{"ULCER", "Non-healing ulcer", "Refer", "ref", json{{"all", {"LESION:ULCER", "DURATION_GE_DAYS:42"}}}};
  auto r = cf::derm::evaluate(f, testConditions(), {sjs, ulcer}, 0);
  CHECK_EQ(r["status"].get<std::string>(), std::string("INSUFFICIENT_DATA"));
  CHECK(r["redFlags"].size() == 1);
  CHECK_EQ(r["redFlags"][0]["code"].get<std::string>(), std::string("SJS"));
  f.lesions = {"ULCER"}; f.durationDays = 30;
  CHECK(!cf::derm::redFlagMatches(ulcer, f, cf::derm::factTokens(f)));
  f.durationDays = 50;
  CHECK(cf::derm::redFlagMatches(ulcer, f, cf::derm::factTokens(f)));
}

TEST(vocabulary_bands) {
  CHECK_EQ(cf::derm::ageBand(7), std::string("0-11"));
  CHECK_EQ(cf::derm::ageBand(65), std::string("60+"));
  CHECK_EQ(*cf::derm::durationDays(3, "WEEKS"), 21);
  CHECK_EQ(cf::derm::durationBand(50), std::string("6w-6m"));
  CHECK(cf::derm::inVocab(cf::derm::lesionTypes(), "WHEAL"));
  CHECK(!cf::derm::inVocab(cf::derm::lesionTypes(), "wheal"));
}

static cf::derm::SafetyMed med(int idx, const std::string& name, const std::string& code, bool reviewed, json rules = json::array()) {
  cf::derm::SafetyMed m;
  m.index = idx; m.name = name; m.genericCode = code; m.ingredients = {code}; m.rules = rules; m.reviewed = reviewed; m.catalogued = true;
  return m;
}

TEST(safety_pregnancy_contraindication_requires_override_only_when_reviewed) {
  cf::derm::SafetyPatient p;
  p.ageYears = 28; p.sex = "FEMALE"; p.pregnancy = "PREGNANT";
  json rules = json::array({{{"tag", "PREGNANCY"}, {"level", "CONTRAINDICATED"}, {"note", "teratogen"}}});
  auto a = cf::derm::checkPrescription(p, {med(0, "Isotretinoin 20 mg", "ISOTRETINOIN", true, rules)}, {});
  CHECK(cf::derm::requiresOverride(a));
  CHECK_EQ(a[0]["level"].get<std::string>(), std::string("CONTRAINDICATED"));
  auto b = cf::derm::checkPrescription(p, {med(0, "Isotretinoin 20 mg", "ISOTRETINOIN", false, rules)}, {});
  CHECK(!cf::derm::requiresOverride(b));
  CHECK_EQ(b[0]["level"].get<std::string>(), std::string("WARNING"));
  CHECK(b[0]["message"].get<std::string>().find("not yet clinically reviewed") != std::string::npos);
  p.pregnancy = "";
  auto c = cf::derm::checkPrescription(p, {med(0, "Isotretinoin 20 mg", "ISOTRETINOIN", true, rules)}, {});
  CHECK_EQ(c[0]["code"].get<std::string>(), std::string("PREGNANCY_STATUS_UNKNOWN"));
}

TEST(safety_allergy_age_duplicates_interactions) {
  cf::derm::SafetyPatient p;
  p.ageYears = 6; p.sex = "MALE"; p.allergies = {"Tetracycline"};
  json age8 = json::array({{{"tag", "AGE_UNDER_8"}, {"level", "CONTRAINDICATED"}}});
  auto doxy = med(0, "Doxycycline 100 mg", "DOXYCYCLINE", true, age8);
  auto iso = med(1, "Isotretinoin 20 mg", "ISOTRETINOIN", true);
  auto doxy2 = med(2, "Doxycycline 100 mg (brand)", "DOXYCYCLINE", true);
  cf::derm::SafetyInteraction ix{"DOXYCYCLINE", "ISOTRETINOIN", "CONTRAINDICATED", "intracranial hypertension", "label", true};
  auto a = cf::derm::checkPrescription(p, {doxy, iso, doxy2}, {ix});
  std::set<std::string> codes;
  for (auto& x : a) codes.insert(x["code"].get<std::string>());
  CHECK(codes.count("ALLERGY"));
  CHECK(codes.count("PATIENT_FACTOR_AGE_UNDER_8"));
  CHECK(codes.count("DUPLICATE_THERAPY"));
  CHECK(codes.count("INTERACTION_CONTRAINDICATED"));
  CHECK_EQ(a[0]["level"].get<std::string>(), std::string("CONTRAINDICATED"));  // most serious first
  p.ageYears = 30; p.allergies.clear();
  auto b = cf::derm::checkPrescription(p, {med(0, "Adapalene gel", "ADAPALENE", true)}, {ix});
  CHECK(b.empty());
}

static cf::derm::PatternCase pcase(const std::string& g, const std::string& doc, const std::string& fac, const std::string& outcome = "") {
  cf::derm::PatternCase c;
  c.generics = {g};
  c.items = json::array({{{"generic", g}, {"name", g}, {"route", "ORAL"}, {"frequency", "Once daily"}, {"durationDays", 14}}});
  c.doctorKey = doc; c.facilityKey = fac; c.outcome = outcome; c.period = "2026-05-01";
  return c;
}

TEST(patterns_own_threshold_and_percentages) {
  std::vector<cf::derm::PatternCase> v = {pcase("A", "me", "f"), pcase("A", "me", "f", "IMPROVED")};
  CHECK_EQ(cf::derm::summarizeOwn(v)["status"].get<std::string>(), std::string("INSUFFICIENT_DATA"));
  v.push_back(pcase("B", "me", "f", "IMPROVED"));
  v.push_back(pcase("A", "me", "f", "WORSENED"));
  auto s = cf::derm::summarizeOwn(v);
  CHECK_EQ(s["status"].get<std::string>(), std::string("OK"));
  CHECK_EQ(s["medications"][0]["generic"].get<std::string>(), std::string("A"));
  CHECK(s["medications"][0]["percent"].get<double>() == 75.0);
  CHECK(s["medications"][0]["outcomes"]["shown"].get<bool>() == false);  // only 2 outcomes for A
}

TEST(patterns_network_disclosure_controls) {
  std::vector<cf::derm::PatternCase> v;
  for (int i = 0; i < 12; ++i) v.push_back(pcase("A", "d" + std::to_string(i % 3), "f1"));
  auto s = cf::derm::summarizeNetwork(v);
  CHECK_EQ(s["status"].get<std::string>(), std::string("INSUFFICIENT_DATA"));  // single clinic
  CHECK(s["caseCount"].is_null());                                             // exact count withheld
  for (int i = 0; i < 6; ++i) v.push_back(pcase("A", "d" + std::to_string(3 + i % 2), "f2"));
  s = cf::derm::summarizeNetwork(v);
  CHECK_EQ(s["status"].get<std::string>(), std::string("OK"));
  CHECK(s["doctorCount"].get<int>() == 5 && s["clinicCount"].get<int>() == 2);
  // A medicine used by a single doctor, or in < 3 cases, is not listed individually
  v.push_back(pcase("RARE", "d0", "f1"));
  v.push_back(pcase("RARE", "d0", "f1"));
  v.push_back(pcase("RARE", "d0", "f1"));
  s = cf::derm::summarizeNetwork(v);
  for (auto& m : s["medications"]) CHECK(m["generic"].get<std::string>() != "RARE");
  // Dominance: > 70 % from one clinic suppresses
  std::vector<cf::derm::PatternCase> dom;
  for (int i = 0; i < 20; ++i) dom.push_back(pcase("A", "d" + std::to_string(i % 4), "f1"));
  for (int i = 0; i < 3; ++i) dom.push_back(pcase("A", "x" + std::to_string(i), "f2"));
  CHECK_EQ(cf::derm::summarizeNetwork(dom)["status"].get<std::string>(), std::string("INSUFFICIENT_DATA"));
}

// ---------------- Handwritten note extraction ----------------
static std::vector<cf::derm::KnownMedicine> testMeds() {
  return {{"FEXOFENADINE", "Fexofenadine"}, {"LULICONAZOLE", "Luliconazole"}, {"BENZOYL_PEROXIDE", "Benzoyl Peroxide"},
          {"TERBINAFINE", "Terbinafine"}, {"EMOLLIENT", "Emollient (Light Liquid Paraffin + White Soft Paraffin)"}};
}
static bool hasCode(const json& arr, const std::string& code) {
  for (auto& x : arr) if (x["code"] == code) return true;
  return false;
}

TEST(extract_structured_note_with_sections_and_shorthand) {
  std::string note =
      "C/o itchy red raised wheals on arms and back x 5/7\n"
      "No fever. No lip swelling\n"
      "O/E multiple erythematous wheals, generalised\n"
      "Imp: ?urticaria\n"
      "Rx tab fexofenadine 120mg OD x 2 weeks";
  auto r = cf::derm::extractClinicalText(note, testMeds());
  auto& a = r["assessment"];
  CHECK(hasCode(a["symptoms"], "ITCHING"));
  CHECK(!hasCode(a["symptoms"], "FEVER"));
  CHECK(hasCode(r["negatives"], "FEVER"));
  CHECK(hasCode(r["negatives"], "LIP_TONGUE_SWELLING"));
  CHECK(hasCode(a["lesionTypes"], "WHEAL"));
  CHECK(hasCode(a["bodySites"], "ARMS") && hasCode(a["bodySites"], "BACK"));
  CHECK(hasCode(a["colours"], "ERYTHEMATOUS"));
  CHECK_EQ(a["distribution"]["code"].get<std::string>(), std::string("GENERALISED"));
  CHECK_EQ(a["duration"]["value"].get<int>(), 5);
  CHECK_EQ(a["duration"]["unit"].get<std::string>(), std::string("DAYS"));  // 5/7 = five days
  CHECK_EQ(a["problemType"]["code"].get<std::string>(), std::string("HIVES_SWELLING"));
  CHECK_EQ(r["diagnoses"][0]["code"].get<std::string>(), std::string("L50.9"));
  CHECK_EQ(r["diagnoses"][0]["writtenAs"].get<std::string>(), std::string("query"));
  CHECK_EQ(r["medicines"][0]["genericCode"].get<std::string>(), std::string("FEXOFENADINE"));
  CHECK_EQ(r["medicines"][0]["frequency"].get<std::string>(), std::string("Once daily"));
  CHECK_EQ(r["medicines"][0]["durationDays"].get<int>(), 14);
  CHECK(r["sections"]["complaint"].get<std::string>().find("itchy") != std::string::npos);
  CHECK(r["sections"]["examination"].get<std::string>().find("wheals") != std::string::npos);
  CHECK(r["sections"]["plan"].get<std::string>().find("fexofenadine") != std::string::npos);
}

TEST(extract_tolerates_recognition_errors_and_suggests_corrections) {
  // typical handwriting-recognition slips: "itcy", "erythematus", "urticarla", "fexofenadlne"
  auto r = cf::derm::extractClinicalText("annular scaly plaque on waist, erythematus edge, 3 weeks\n?tinea corporis\nluliconazol cream", testMeds());
  CHECK(hasCode(r["assessment"]["colours"], "ERYTHEMATOUS"));
  bool corrected = false;
  for (auto& c : r["corrections"]) if (c["original"] == "erythematus") corrected = true;
  CHECK(corrected);
  CHECK_EQ(r["assessment"]["configuration"]["code"].get<std::string>(), std::string("ANNULAR"));
  CHECK_EQ(r["assessment"]["duration"]["unit"].get<std::string>(), std::string("WEEKS"));
  CHECK_EQ(r["diagnoses"][0]["code"].get<std::string>(), std::string("B35.4"));  // longest synonym wins over "tinea"
  CHECK_EQ(r["medicines"][0]["genericCode"].get<std::string>(), std::string("LULICONAZOLE"));
  CHECK(r["medicines"][0]["fuzzy"].get<bool>());
}

TEST(extract_without_headings_and_negated_diagnosis) {
  auto r = cf::derm::extractClinicalText("Pimples and blackheads on face since 6 months, mild. Not scabies. Not pregnant.", testMeds());
  CHECK_EQ(r["assessment"]["problemType"]["code"].get<std::string>(), std::string("ACNE_BREAKOUTS"));
  CHECK(hasCode(r["assessment"]["lesionTypes"], "COMEDONE"));
  CHECK_EQ(r["assessment"]["severity"]["code"].get<std::string>(), std::string("MILD"));
  CHECK_EQ(r["assessment"]["duration"]["value"].get<int>(), 6);
  CHECK(r["diagnoses"].empty());
  CHECK_EQ(r["excludedDiagnoses"][0]["code"].get<std::string>(), std::string("B86"));
  CHECK_EQ(r["assessment"]["pregnancyStatus"]["code"].get<std::string>(), std::string("NOT_PREGNANT"));
  CHECK(r["sections"].contains("complaint"));
}

TEST(extract_does_not_invent_findings) {
  auto r = cf::derm::extractClinicalText("Patient came back for review. Advised to continue.", testMeds());
  CHECK(r["assessment"]["symptoms"].empty() && r["assessment"]["lesionTypes"].empty());
  CHECK(r["diagnoses"].empty() && r["medicines"].empty());
  CHECK(r["assessment"]["duration"].is_null());
  auto e = cf::derm::extractClinicalText("   ", testMeds());
  CHECK(!e["warnings"].empty());
  CHECK_EQ(cf::derm::editDistance("urticaria", "urticarla"), 1);
}

TEST(recognizer_response_parsing) {
  auto ok = cf::hwr::GoogleVisionRecognizer::parseResponse(200, R"({"responses":[{"fullTextAnnotation":{"text":"c/o itchy wheals\n"}}]})");
  CHECK(ok.ok && ok.text == "c/o itchy wheals\n");
  auto err = cf::hwr::GoogleVisionRecognizer::parseResponse(200, R"({"responses":[{"error":{"code":3,"message":"Bad image data."}}]})");
  CHECK(!err.ok && err.error.find("Bad image data") != std::string::npos);
  auto denied = cf::hwr::GoogleVisionRecognizer::parseResponse(403, R"({"error":{"code":403,"message":"API key not valid"}})");
  CHECK(!denied.ok && denied.error.find("403") != std::string::npos);
  auto empty = cf::hwr::GoogleVisionRecognizer::parseResponse(200, R"({"responses":[{}]})");
  CHECK(empty.ok && empty.text.empty());
  auto h = cf::hwr::HttpRecognizer::parseResponse(200, R"({"text":"no fever"})");
  CHECK(h.ok && h.text == "no fever");
  CHECK(!cf::hwr::HttpRecognizer::parseResponse(200, R"({"words":[]})").ok);
  CHECK(!cf::hwr::HttpRecognizer::parseResponse(500, "oops").ok);
  CHECK_EQ(cf::hwr::decodeChunked("4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n"), std::string("Wikipedia"));
  cf::hwr::DisabledRecognizer d;
  CHECK(!d.enabled() && !d.sendsDataExternally());
}

int main() {
  for (auto& [name, fn] : registry()) {
    try {
      fn();
      ++g_passed;
      std::cout << "  PASS  " << name << "\n";
    } catch (const std::exception& e) {
      ++g_failed;
      std::cout << "  FAIL  " << name << "\n        " << e.what() << "\n";
    }
  }
  std::cout << "\n" << g_passed << " passed, " << g_failed << " failed\n";
  return g_failed == 0 ? 0 : 1;
}
