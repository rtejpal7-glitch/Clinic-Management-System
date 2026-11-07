// Unit tests for pure backend logic (no database or network).
// Run: ctest --test-dir build  (or ./build/clinicflow-unit-tests)

#include <functional>
#include <iostream>
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
