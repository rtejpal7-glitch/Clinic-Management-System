#include "FhirMapper.hpp"

#include <functional>
#include <map>
#include <regex>
#include <set>

#include "common/ApiError.hpp"
#include "common/Text.hpp"

namespace cf {

using json = nlohmann::json;

namespace {
const char* SYS_PATIENT_NUMBER = "urn:clinicflow:patient-number";       // facility-local MRN namespace
const char* SYS_ABHA_NUMBER = "urn:clinicflow:placeholder:abha-number"; // replace with the ABDM-mandated system URI
const char* SYS_ABHA_ADDRESS = "urn:clinicflow:placeholder:abha-address";
const char* SYS_REG = "urn:clinicflow:medical-council-registration";
const char* SYS_HFR = "urn:clinicflow:placeholder:hfr-id";
const char* SNOMED = "http://snomed.info/sct";
const char* LOINC = "http://loinc.org";
const char* ICD10 = "http://hl7.org/fhir/sid/icd-10";
const char* UCUM = "http://unitsofmeasure.org";

std::string ref(const std::string& type, const std::string& id) { return "urn:uuid:" + id; }
json meta(const std::string& profile) {
  json m = {{"profile", json::array({std::string(FhirMapper::NRCES) + profile})}};
  return m;
}
json coding(const char* system, const std::string& code, const std::string& display) {
  return {{"system", system}, {"code", code}, {"display", display}};
}
std::string genderCode(const std::string& g) {
  if (g == "MALE") return "male";
  if (g == "FEMALE") return "female";
  if (g == "OTHER") return "other";
  return "unknown";
}
json quantity(double v, const char* unit, const char* code) {
  return {{"value", v}, {"unit", unit}, {"system", UCUM}, {"code", code}};
}
}  // namespace

json FhirMapper::patientFrom(Conn& c, const std::string& id) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Patient");
  auto r = c.exec("SELECT *, date_of_birth::text AS dob FROM patients WHERE id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Patient");
  auto p = r.first();
  json res = {{"resourceType", "Patient"}, {"id", id}, {"meta", meta("Patient")},
              {"identifier", json::array({{{"type", {{"coding", json::array({coding("http://terminology.hl7.org/CodeSystem/v2-0203", "MR", "Medical record number")})}}},
                                           {"system", SYS_PATIENT_NUMBER}, {"value", p.str("patient_number")}}})},
              {"name", json::array({{{"text", p.str("full_name")}}})},
              {"gender", genderCode(p.str("gender"))},
              {"birthDate", p.str("dob")},
              {"telecom", json::array({{{"system", "phone"}, {"value", p.str("mobile")}, {"use", "mobile"}}})}};
  if (!p.isNull("email")) res["telecom"].push_back({{"system", "email"}, {"value", p.str("email")}});
  if (!p.isNull("address_line") || !p.isNull("city")) {
    json addr = {{"use", "home"}, {"country", "IN"}};
    if (!p.isNull("address_line")) addr["line"] = json::array({p.str("address_line")});
    if (!p.isNull("city")) addr["city"] = p.str("city");
    if (!p.isNull("district")) addr["district"] = p.str("district");
    if (!p.isNull("state")) addr["state"] = p.str("state");
    if (!p.isNull("pincode")) addr["postalCode"] = p.str("pincode");
    res["address"] = json::array({addr});
  }
  bool anyMock = false;
  c.exec("SELECT id_type, value_ciphertext, is_mock FROM patient_identifiers WHERE patient_id = $1 AND verification_status = 'VERIFIED'",
         Params(id))
      .each([&](const Row& x) {
        std::string type = x.str("id_type");
        res["identifier"].push_back({{"system", type == "ABHA_NUMBER" ? SYS_ABHA_NUMBER : SYS_ABHA_ADDRESS},
                                     {"value", m_cipher->decrypt(x.str("value_ciphertext"))},
                                     {"type", {{"text", type == "ABHA_NUMBER" ? "ABHA Number" : "ABHA Address"}}}});
        anyMock = anyMock || x.boolv("is_mock");
      });
  if (anyMock)
    res["meta"]["tag"] = json::array({{{"system", "urn:clinicflow:tags"}, {"code", "MOCK-ABDM"},
                                       {"display", "ABHA verification simulated by mock adapter"}}});
  return res;
}

json FhirMapper::practitionerFrom(Conn& c, const std::string& id) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Doctor");
  auto r = c.exec("SELECT * FROM doctors WHERE id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Doctor");
  auto d = r.first();
  json res = {{"resourceType", "Practitioner"}, {"id", id}, {"meta", meta("Practitioner")},
              {"name", json::array({{{"text", d.str("display_name")}}})}, {"identifier", json::array()}};
  if (!d.isNull("registration_number"))
    res["identifier"].push_back({{"type", {{"coding", json::array({coding("http://terminology.hl7.org/CodeSystem/v2-0203", "MD", "Medical License number")})}}},
                                 {"system", SYS_REG}, {"value", d.str("registration_number")}});
  if (!d.isNull("qualification")) res["qualification"] = json::array({{{"code", {{"text", d.str("qualification")}}}}});
  if (res["identifier"].empty()) res.erase("identifier");
  return res;
}

json FhirMapper::organizationFrom(Conn& c) const {
  auto s = c.exec("SELECT * FROM clinic_settings WHERE id = 1").first();
  // Deterministic id for the single clinic organization.
  json res = {{"resourceType", "Organization"}, {"id", "00000000-0000-4000-8000-000000000001"}, {"meta", meta("Organization")},
              {"name", s.str("clinic_name")}};
  if (!s.isNull("hfr_id")) res["identifier"] = json::array({{{"system", SYS_HFR}, {"value", s.str("hfr_id")}}});
  if (!s.isNull("phone")) res["telecom"] = json::array({{{"system", "phone"}, {"value", s.str("phone")}}});
  return res;
}

json FhirMapper::patient(const std::string& id) const { return m_db->withConn([&](Conn& c) { return patientFrom(c, id); }); }
json FhirMapper::practitioner(const std::string& id) const { return m_db->withConn([&](Conn& c) { return practitionerFrom(c, id); }); }
json FhirMapper::organization() const { return m_db->withConn([&](Conn& c) { return organizationFrom(c); }); }

json FhirMapper::appointment(const std::string& id) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Appointment");
  return m_db->withConn([&](Conn& c) {
    auto r = c.exec("SELECT a.*, p.full_name, d.display_name FROM appointments a JOIN patients p ON p.id = a.patient_id "
                    "JOIN doctors d ON d.id = a.doctor_id WHERE a.id = $1", Params(id));
    if (r.empty()) throw ApiError::notFound("Appointment");
    auto a = r.first();
    static const std::map<std::string, std::string> st = {{"SCHEDULED", "booked"}, {"CHECKED_IN", "checked-in"},
                                                          {"IN_CONSULTATION", "fulfilled"}, {"COMPLETED", "fulfilled"},
                                                          {"CANCELLED", "cancelled"}, {"NO_SHOW", "noshow"}};
    json res = {{"resourceType", "Appointment"}, {"id", id}, {"status", st.at(a.str("status"))},
                {"start", *a.ts("start_at")}, {"end", *a.ts("end_at")},
                {"appointmentType", {{"text", a.str("appointment_type")}}},
                {"participant", json::array({
                    {{"actor", {{"reference", "Patient/" + a.str("patient_id")}, {"display", a.str("full_name")}}}, {"status", "accepted"}},
                    {{"actor", {{"reference", "Practitioner/" + a.str("doctor_id")}, {"display", a.str("display_name")}}}, {"status", "accepted"}}})}};
    if (!a.isNull("reason")) res["description"] = a.str("reason");
    if (!a.isNull("cancellation_reason")) res["cancelationReason"] = {{"text", a.str("cancellation_reason")}};
    return res;
  });
}

json FhirMapper::encounter(const std::string& consultationId) const {
  auto b = consultationBundle(consultationId);
  for (auto& e : b["entry"])
    if (e["resource"]["resourceType"] == "Encounter") return e["resource"];
  throw ApiError::notFound("Encounter");
}

json FhirMapper::invoice(const std::string& billId) const {
  if (!text::isUuid(billId)) throw ApiError::notFound("Bill");
  return m_db->withConn([&](Conn& c) {
    auto r = c.exec("SELECT b.*, p.full_name FROM bills b JOIN patients p ON p.id = b.patient_id WHERE b.id = $1", Params(billId));
    if (r.empty()) throw ApiError::notFound("Bill");
    auto b = r.first();
    auto money = [](long long paise) { return json{{"value", paise / 100.0}, {"currency", "INR"}}; };
    std::string st = b.str("status");
    json res = {{"resourceType", "Invoice"}, {"id", billId},
                {"identifier", json::array({{{"system", "urn:clinicflow:invoice-number"}, {"value", b.str("invoice_number")}}})},
                {"status", st == "CANCELLED" ? "cancelled" : (st == "PENDING" ? "issued" : "balanced")},
                {"subject", {{"reference", "Patient/" + b.str("patient_id")}, {"display", b.str("full_name")}}},
                {"date", *b.ts("created_at")},
                {"totalNet", money(b.i64v("subtotal_paise") - b.i64v("discount_paise"))},
                {"totalGross", money(b.i64v("total_paise"))}, {"lineItem", json::array()}};
    int seq = 1;
    c.exec("SELECT * FROM bill_items WHERE bill_id = $1 ORDER BY sort_order", Params(billId)).each([&](const Row& i) {
      res["lineItem"].push_back(
          {{"sequence", seq++}, {"chargeItemCodeableConcept", {{"text", i.str("description")}}},
           {"priceComponent", json::array({{{"type", "base"}, {"amount", money(i.i64v("line_subtotal_paise"))}},
                                           {{"type", "discount"}, {"amount", money(i.i64v("line_discount_paise"))}},
                                           {{"type", "tax"}, {"amount", money(i.i64v("line_tax_paise"))}}})}});
    });
    return res;
  });
}

json FhirMapper::consultationBundle(const std::string& cid) const {
  if (!text::isUuid(cid)) throw ApiError::notFound("Consultation");
  return m_db->withConn([&](Conn& c) {
    auto cr = c.exec("SELECT *, follow_up_date::text AS fud FROM consultations WHERE id = $1", Params(cid));
    if (cr.empty()) throw ApiError::notFound("Consultation");
    auto con = cr.first();
    std::string pid = con.str("patient_id"), did = con.str("doctor_id");
    json patientRes = patientFrom(c, pid);
    json practRes = practitionerFrom(c, did);
    json orgRes = organizationFrom(c);
    std::string orgId = orgRes["id"];
    json subject = {{"reference", ref("Patient", pid)}, {"display", patientRes["name"][0]["text"]}};
    json authorRef = {{"reference", ref("Practitioner", did)}, {"display", practRes["name"][0]["text"]}};
    std::string started = *con.ts("started_at");
    std::string ended = con.isNull("finalized_at") ? started : std::string(*con.ts("finalized_at"));

    std::vector<json> entries;
    auto add = [&](const json& res) { entries.push_back({{"fullUrl", "urn:uuid:" + res["id"].get<std::string>()}, {"resource", res}}); };

    std::string encId = cid;  // encounter shares the consultation id
    json enc = {{"resourceType", "Encounter"}, {"id", encId}, {"meta", meta("Encounter")},
                {"status", con.str("status") == "FINALIZED" ? "finished" : "in-progress"},
                {"class", {{"system", "http://terminology.hl7.org/CodeSystem/v3-ActCode"}, {"code", "AMB"}, {"display", "ambulatory"}}},
                {"subject", subject},
                {"participant", json::array({{{"individual", authorRef}}})},
                {"period", {{"start", started}, {"end", ended}}},
                {"serviceProvider", {{"reference", ref("Organization", orgId)}}}};

    std::vector<json> conditions, observations, meds, orders, allergies;
    c.exec("SELECT * FROM consultation_diagnoses WHERE consultation_id = $1 ORDER BY sort_order", Params(cid)).each([&](const Row& d) {
      json code = {{"text", d.str("display")}};
      std::string sys = d.str("code_system");
      if (sys == "ICD-10") code["coding"] = json::array({coding(ICD10, d.str("code"), d.str("display"))});
      if (sys == "SNOMED-CT") code["coding"] = json::array({coding(SNOMED, d.str("code"), d.str("display"))});
      bool provisional = d.str("diagnosis_type") == "PROVISIONAL";
      conditions.push_back(
          {{"resourceType", "Condition"}, {"id", d.str("id")}, {"meta", meta("Condition")},
           {"clinicalStatus", {{"coding", json::array({coding("http://terminology.hl7.org/CodeSystem/condition-clinical", "active", "Active")})}}},
           {"verificationStatus", {{"coding", json::array({coding("http://terminology.hl7.org/CodeSystem/condition-ver-status",
                                                                   provisional ? "provisional" : "confirmed",
                                                                   provisional ? "Provisional" : "Confirmed")})}}},
           {"category", json::array({{{"coding", json::array({coding("http://terminology.hl7.org/CodeSystem/condition-category", "encounter-diagnosis", "Encounter Diagnosis")})}}})},
           {"code", code}, {"subject", subject}, {"encounter", {{"reference", ref("Encounter", encId)}}},
           {"recordedDate", started}});
    });

    auto vit = c.exec("SELECT * FROM vital_signs WHERE consultation_id = $1", Params(cid));
    if (!vit.empty()) {
      auto v = vit.first();
      std::string base = v.str("id");
      int n = 0;
      auto obs = [&](const std::string& loinc, const std::string& display, json valueOrComponents, bool isComponent = false) {
        // derive a stable UUID-like id per observation from the vitals row id
        std::string id = base.substr(0, base.size() - 2) + (n < 10 ? "0" : "") + std::to_string(n);
        ++n;
        json o = {{"resourceType", "Observation"}, {"id", id},
                  {"meta", {{"profile", json::array({"http://hl7.org/fhir/StructureDefinition/vitalsigns"})}}},
                  {"status", "final"},
                  {"category", json::array({{{"coding", json::array({coding("http://terminology.hl7.org/CodeSystem/observation-category", "vital-signs", "Vital Signs")})}}})},
                  {"code", {{"coding", json::array({coding(LOINC, loinc, display)})}, {"text", display}}},
                  {"subject", subject}, {"encounter", {{"reference", ref("Encounter", encId)}}},
                  {"effectiveDateTime", *v.ts("recorded_at")}, {"performer", json::array({authorRef})}};
        if (isComponent) o["component"] = valueOrComponents;
        else o["valueQuantity"] = valueOrComponents;
        observations.push_back(o);
      };
      if (!v.isNull("bp_systolic") && !v.isNull("bp_diastolic")) {
        obs("85354-9", "Blood pressure panel with all children optional",
            json::array({{{"code", {{"coding", json::array({coding(LOINC, "8480-6", "Systolic blood pressure")})}}},
                          {"valueQuantity", quantity((double)v.i64v("bp_systolic"), "mmHg", "mm[Hg]")}},
                         {{"code", {{"coding", json::array({coding(LOINC, "8462-4", "Diastolic blood pressure")})}}},
                          {"valueQuantity", quantity((double)v.i64v("bp_diastolic"), "mmHg", "mm[Hg]")}}}),
            true);
      }
      if (!v.isNull("pulse")) obs("8867-4", "Heart rate", quantity((double)v.i64v("pulse"), "beats/minute", "/min"));
      if (!v.isNull("temperature_c")) obs("8310-5", "Body temperature", quantity(*v.f64("temperature_c"), "C", "Cel"));
      if (!v.isNull("respiratory_rate")) obs("9279-1", "Respiratory rate", quantity((double)v.i64v("respiratory_rate"), "breaths/minute", "/min"));
      if (!v.isNull("spo2")) obs("59408-5", "Oxygen saturation in Arterial blood by Pulse oximetry", quantity((double)v.i64v("spo2"), "%", "%"));
      if (!v.isNull("height_cm")) obs("8302-2", "Body height", quantity(*v.f64("height_cm"), "cm", "cm"));
      if (!v.isNull("weight_kg")) obs("29463-7", "Body weight", quantity(*v.f64("weight_kg"), "kg", "kg"));
    }

    c.exec("SELECT * FROM patient_allergies WHERE patient_id = $1 AND status = 'ACTIVE'", Params(pid)).each([&](const Row& a) {
      std::string sev = a.str("severity");
      json res = {{"resourceType", "AllergyIntolerance"}, {"id", a.str("id")}, {"meta", meta("AllergyIntolerance")},
                  {"clinicalStatus", {{"coding", json::array({coding("http://terminology.hl7.org/CodeSystem/allergyintolerance-clinical", "active", "Active")})}}},
                  {"category", json::array({text::lower(a.str("category")) == "medication" ? "medication"
                                            : text::lower(a.str("category")) == "food"     ? "food"
                                                                                            : "environment"})},
                  {"code", {{"text", a.str("substance")}}}, {"patient", subject}, {"recordedDate", *a.ts("created_at")}};
      if (sev == "SEVERE") res["criticality"] = "high";
      else if (sev == "MILD" || sev == "MODERATE") res["criticality"] = "low";
      if (!a.isNull("reaction")) res["reaction"] = json::array({{{"manifestation", json::array({{{"text", a.str("reaction")}}})}}});
      if (a.str("category") == "OTHER") res.erase("category");
      allergies.push_back(res);
    });

    auto rx = c.exec("SELECT * FROM prescriptions WHERE consultation_id = $1", Params(cid));
    std::string rxStatus = rx.empty() ? "DRAFT" : rx.first().str("status");
    std::string advice = rx.empty() ? "" : rx.first().str("advice");
    if (!rx.empty()) {
      c.exec("SELECT * FROM prescription_items WHERE prescription_id = $1 ORDER BY sort_order", Params(rx.first().str("id")))
          .each([&](const Row& i) {
            std::string medText = i.str("medicine_name") + (i.isNull("strength") ? "" : " " + i.str("strength")) +
                                  (i.isNull("dosage_form") ? "" : " " + i.str("dosage_form"));
            std::string dosageText = i.str("dose") + ", " + i.str("frequency") +
                                     (i.isNull("duration_days") ? "" : ", for " + i.str("duration_days") + " days") +
                                     (i.isNull("instructions") ? "" : " — " + i.str("instructions"));
            json mr = {{"resourceType", "MedicationRequest"}, {"id", i.str("id")}, {"meta", meta("MedicationRequest")},
                       {"status", rxStatus == "FINALIZED" ? "active" : "draft"}, {"intent", "order"},
                       {"medicationCodeableConcept", {{"text", medText}}}, {"subject", subject},
                       {"encounter", {{"reference", ref("Encounter", encId)}}}, {"authoredOn", started}, {"requester", authorRef},
                       {"dosageInstruction", json::array({{{"text", dosageText}, {"route", {{"text", i.str("route")}}},
                                                           {"timing", {{"code", {{"text", i.str("frequency")}}}}}}})}};
            if (!i.isNull("quantity") || !i.isNull("duration_days")) {
              json dr = json::object();
              if (!i.isNull("quantity")) dr["quantity"] = {{"value", *i.f64("quantity")}};
              if (!i.isNull("duration_days"))
                dr["expectedSupplyDuration"] = {{"value", i.i64v("duration_days")}, {"unit", "days"}, {"system", UCUM}, {"code", "d"}};
              mr["dispenseRequest"] = dr;
            }
            meds.push_back(mr);
          });
    }
    c.exec("SELECT * FROM investigation_orders WHERE consultation_id = $1 ORDER BY ordered_at", Params(cid)).each([&](const Row& o) {
      json code = {{"text", o.str("name")}};
      if (!o.isNull("loinc_code")) code["coding"] = json::array({coding(LOINC, o.str("loinc_code"), o.str("name"))});
      std::string st = o.str("status");
      orders.push_back({{"resourceType", "ServiceRequest"}, {"id", o.str("id")}, {"meta", meta("ServiceRequest")},
                        {"status", st == "CANCELLED" ? "revoked" : (st == "COMPLETED" ? "completed" : (rxStatus == "FINALIZED" ? "active" : "draft"))},
                        {"intent", "order"}, {"priority", text::lower(o.str("priority"))}, {"code", code}, {"subject", subject},
                        {"encounter", {{"reference", ref("Encounter", encId)}}}, {"authoredOn", *o.ts("ordered_at")},
                        {"requester", authorRef}});
    });

    // Composition (document header) — OP consultation record
    auto section = [&](const std::string& title, const std::string& snomed, const std::string& display,
                       const std::vector<json>& resources, const std::string& narrative) {
      json s = {{"title", title}, {"code", {{"coding", json::array({coding(SNOMED, snomed, display)})}}}};
      if (!resources.empty()) {
        s["entry"] = json::array();
        for (auto& r : resources) s["entry"].push_back({{"reference", "urn:uuid:" + r["id"].get<std::string>()}});
      }
      if (!narrative.empty())
        s["text"] = {{"status", "generated"}, {"div", "<div xmlns=\"http://www.w3.org/1999/xhtml\">" + narrative + "</div>"}};
      return s;
    };
    auto esc = [](std::string s) {
      std::string o;
      for (char ch : s) {
        if (ch == '<') o += "&lt;";
        else if (ch == '>') o += "&gt;";
        else if (ch == '&') o += "&amp;";
        else o += ch;
      }
      return o;
    };
    json sections = json::array();
    if (!con.isNull("chief_complaint")) sections.push_back(section("Chief complaints", "422843007", "Chief complaint section", {}, esc(con.str("chief_complaint"))));
    if (!allergies.empty()) sections.push_back(section("Allergies", "722446000", "Allergy record", allergies, ""));
    std::string hx = (con.isNull("history_present_illness") ? "" : "HPI: " + esc(con.str("history_present_illness")) + " ") +
                     (con.isNull("past_medical_history") ? "" : "Past history: " + esc(con.str("past_medical_history")));
    if (!hx.empty() || !conditions.empty()) sections.push_back(section("Medical history and diagnoses", "371529009", "History and physical report", conditions, hx));
    if (!observations.empty() || !con.isNull("examination_notes"))
      sections.push_back(section("Physical examination", "425044008", "Physical exam section", observations, esc(con.str("examination_notes"))));
    if (!orders.empty()) sections.push_back(section("Investigation advice", "721963009", "Order document", orders, ""));
    if (!meds.empty() || !advice.empty()) sections.push_back(section("Medications", "721912009", "Medication summary document", meds, esc(advice)));
    std::string fu = (con.isNull("treatment_plan") ? "" : "Plan: " + esc(con.str("treatment_plan")) + " ") +
                     (con.isNull("follow_up_instructions") ? "" : "Follow-up: " + esc(con.str("follow_up_instructions")) + " ") +
                     (con.isNull("fud") ? "" : "Review on " + con.str("fud"));
    if (!fu.empty()) sections.push_back(section("Follow up", "736271009", "Outpatient care plan", {}, fu));

    std::string compId = c.exec("SELECT md5('composition:' || $1::text)::uuid::text AS u", Params(cid)).first().str("u");
    json comp = {{"resourceType", "Composition"}, {"id", compId}, {"meta", meta("OPConsultRecord")},
                 {"status", con.str("status") == "FINALIZED" ? "final" : "preliminary"},
                 {"type", {{"coding", json::array({coding(SNOMED, "371530004", "Clinical consultation report")})}, {"text", "Clinical consultation report"}}},
                 {"subject", subject}, {"encounter", {{"reference", ref("Encounter", encId)}}}, {"date", ended},
                 {"author", json::array({authorRef})}, {"title", "Consultation Report"},
                 {"custodian", {{"reference", ref("Organization", orgId)}}}, {"section", sections}};

    add(comp);
    add(patientRes);
    add(practRes);
    add(orgRes);
    add(enc);
    for (auto* group : {&conditions, &observations, &allergies, &meds, &orders})
      for (auto& r : *group) add(r);

    std::string bundleId = c.exec("SELECT md5('bundle:' || $1::text || coalesce($2, ''))::uuid::text AS u",
                                  Params(cid, con.opt("finalized_at"))).first().str("u");
    json bundle = {{"resourceType", "Bundle"}, {"id", bundleId}, {"meta", meta("DocumentBundle")},
                   {"identifier", {{"system", "urn:clinicflow:document"}, {"value", bundleId}}},
                   {"type", "document"}, {"timestamp", ended}, {"entry", entries}};
    bundle["meta"]["lastUpdated"] = ended;
    return bundle;
  });
}

// ---------------- Validation ----------------
static void checkCodings(const json& node, const std::string& path, std::vector<std::string>& issues) {
  if (node.is_object()) {
    if (node.contains("coding")) {
      for (auto& cd : node["coding"]) {
        if (!cd.contains("system") || !cd.contains("code"))
          issues.push_back(path + ".coding: system and code are required");
      }
    }
    for (auto& [k, v] : node.items()) checkCodings(v, path + "." + k, issues);
  } else if (node.is_array()) {
    for (size_t i = 0; i < node.size(); ++i) checkCodings(node[i], path + "[" + std::to_string(i) + "]", issues);
  }
}

std::vector<std::string> FhirMapper::validateResource(const json& r) {
  std::vector<std::string> issues;
  if (!r.is_object() || !r.contains("resourceType")) return {"resourceType is required"};
  std::string t = r["resourceType"];
  static const std::regex idRe("^[A-Za-z0-9\\-\\.]{1,64}$");
  if (!r.contains("id") || !std::regex_match(r["id"].get<std::string>(), idRe)) issues.push_back(t + ".id is missing or invalid");
  auto need = [&](std::initializer_list<const char*> fields) {
    for (auto f : fields)
      if (!r.contains(f)) issues.push_back(t + "." + f + " is required");
  };
  if (t == "Patient") need({"name", "gender", "identifier"});
  else if (t == "Practitioner") need({"name"});
  else if (t == "Organization") need({"name"});
  else if (t == "Encounter") need({"status", "class", "subject"});
  else if (t == "Condition") need({"code", "subject"});
  else if (t == "Observation") {
    need({"status", "code", "subject"});
    if (!r.contains("valueQuantity") && !r.contains("component")) issues.push_back("Observation needs a value or components");
  } else if (t == "MedicationRequest") need({"status", "intent", "subject", "medicationCodeableConcept"});
  else if (t == "ServiceRequest") need({"status", "intent", "subject", "code"});
  else if (t == "AllergyIntolerance") need({"code", "patient"});
  else if (t == "Composition") need({"status", "type", "subject", "date", "author", "title"});
  else if (t == "Appointment") need({"status", "participant"});
  else if (t == "Invoice") need({"status", "subject"});
  if (r.contains("birthDate") && !text::isIsoDate(r["birthDate"].get<std::string>())) issues.push_back("Patient.birthDate must be YYYY-MM-DD");
  if (r.contains("gender")) {
    std::string g = r["gender"];
    if (g != "male" && g != "female" && g != "other" && g != "unknown") issues.push_back(t + ".gender has an invalid code");
  }
  checkCodings(r, t, issues);
  return issues;
}

std::vector<std::string> FhirMapper::validateBundle(const json& b) {
  std::vector<std::string> issues;
  if (b.value("resourceType", "") != "Bundle") return {"resourceType must be Bundle"};
  if (b.value("type", "") != "document") issues.push_back("Bundle.type must be 'document'");
  if (!b.contains("timestamp")) issues.push_back("Bundle.timestamp is required for documents");
  if (!b.contains("identifier")) issues.push_back("Bundle.identifier is required for documents");
  if (!b.contains("entry") || b["entry"].empty()) return {"Bundle has no entries"};
  if (b["entry"][0]["resource"].value("resourceType", "") != "Composition")
    issues.push_back("First entry of a document bundle must be a Composition");
  std::set<std::string> fullUrls;
  for (auto& e : b["entry"]) {
    if (!e.contains("fullUrl")) issues.push_back("entry.fullUrl is required");
    else if (!fullUrls.insert(e["fullUrl"].get<std::string>()).second) issues.push_back("Duplicate fullUrl " + e["fullUrl"].get<std::string>());
    for (auto& i : validateResource(e["resource"])) issues.push_back(i);
  }
  // Every urn:uuid reference must resolve inside the bundle.
  std::function<void(const json&)> walk = [&](const json& n) {
    if (n.is_object()) {
      if (n.contains("reference") && n["reference"].is_string()) {
        std::string ref = n["reference"];
        if (ref.rfind("urn:uuid:", 0) == 0 && !fullUrls.count(ref)) issues.push_back("Unresolved reference " + ref);
      }
      for (auto& [k, v] : n.items()) walk(v);
    } else if (n.is_array()) {
      for (auto& v : n) walk(v);
    }
  };
  walk(b);
  return issues;
}

}  // namespace cf
