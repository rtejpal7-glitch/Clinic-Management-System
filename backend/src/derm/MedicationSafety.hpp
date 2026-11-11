#pragma once
// Prescription safety checks (pure logic). Inputs are loaded by the service from
// the patient record and the medicine catalogue.
//
// Levels:
//   CONTRAINDICATED  — from clinically APPROVED content or a recorded patient allergy.
//                      Finalizing requires a documented override reason.
//   WARNING          — cautions, duplicates, major interactions, and any alert that
//                      comes from content that has not been clinically reviewed yet.
//   INFO             — high-alert medicine reminders, antimicrobial stewardship notes,
//                      missing information (e.g. pregnancy status not recorded).

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace cf::derm {

struct SafetyPatient {
  std::optional<int> ageYears;
  std::string sex;        // MALE | FEMALE | OTHER | UNDISCLOSED
  std::string pregnancy;  // PREGNANT | BREASTFEEDING | NOT_PREGNANT | NOT_APPLICABLE | UNKNOWN | ""
  std::vector<std::string> allergies;  // active medication allergy substances
};

struct SafetyMed {
  int index = 0;  // position in the prescription
  std::string name, genericCode;
  std::vector<std::string> ingredients;
  nlohmann::json rules = nlohmann::json::array();
  bool highRisk = false, restricted = false;
  std::string aware;
  bool reviewed = false;  // catalogue row clinically APPROVED
  bool catalogued = false;
};

struct SafetyInteraction {
  std::string a, b, severity, description, reference;
  bool reviewed = false;
};

nlohmann::json checkPrescription(const SafetyPatient& p, const std::vector<SafetyMed>& meds,
                                 const std::vector<SafetyInteraction>& interactions);

// True when any alert needs an override reason before the prescription can be finalized.
bool requiresOverride(const nlohmann::json& alerts);

}  // namespace cf::derm
