#include "MedicationSafety.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace cf::derm {

using json = nlohmann::json;

static std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
static std::string humanCode(std::string code) {
  for (auto& c : code) c = c == '_' ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return code;
}

// Does a recorded allergy substance plausibly refer to this medicine?
static bool allergyMatches(const std::string& allergy, const SafetyMed& m) {
  std::string a = lower(allergy);
  if (a.size() < 4) return false;
  std::vector<std::string> names = {lower(m.name)};
  for (auto& i : m.ingredients) names.push_back(humanCode(i));
  if (!m.genericCode.empty()) names.push_back(humanCode(m.genericCode));
  for (auto& n : names) {
    if (n.empty()) continue;
    if (n.find(a) != std::string::npos || (n.size() >= 4 && a.find(n) != std::string::npos)) return true;
  }
  // Class names commonly recorded as allergies
  static const std::vector<std::pair<std::string, std::vector<std::string>>> classes = {
      {"tetracycline", {"DOXYCYCLINE", "MINOCYCLINE"}},
      {"sulfa", {"SULFAMETHOXAZOLE"}},
      {"azole", {"KETOCONAZOLE_TOP", "KETOCONAZOLE_SHAMPOO", "ITRACONAZOLE", "FLUCONAZOLE", "LULICONAZOLE", "CLOTRIMAZOLE"}},
      {"nsaid", {"IBUPROFEN", "DICLOFENAC"}},
      {"steroid", {"PREDNISOLONE"}}};
  for (auto& [cls, members] : classes)
    if (a.find(cls) != std::string::npos && std::find(members.begin(), members.end(), m.genericCode) != members.end()) return true;
  return false;
}

static bool tagApplies(const std::string& tag, const SafetyPatient& p) {
  if (tag == "PREGNANCY") return p.pregnancy == "PREGNANT";
  if (tag == "BREASTFEEDING") return p.pregnancy == "BREASTFEEDING";
  if (tag.rfind("AGE_UNDER_", 0) == 0 && p.ageYears) {
    try {
      return *p.ageYears < std::stoi(tag.substr(10));
    } catch (...) {
      return false;
    }
  }
  return false;
}

json checkPrescription(const SafetyPatient& p, const std::vector<SafetyMed>& meds,
                       const std::vector<SafetyInteraction>& interactions) {
  json alerts = json::array();
  auto add = [&](const std::string& level, const std::string& code, const std::string& msg, std::vector<int> items,
                 bool reviewed, const std::string& reference = "") {
    json a = {{"level", level}, {"code", code}, {"message", msg}, {"items", items}, {"reviewedContent", reviewed}};
    if (!reference.empty()) a["reference"] = reference;
    a["requiresOverride"] = level == "CONTRAINDICATED" || code == "RESTRICTED_ANTIMICROBIAL";
    alerts.push_back(a);
  };

  bool femaleOfChildbearingAge = p.sex == "FEMALE" && p.ageYears && *p.ageYears >= 12 && *p.ageYears <= 50;
  bool pregnancyUnknown = p.pregnancy.empty() || p.pregnancy == "UNKNOWN";

  for (auto& m : meds) {
    // Allergies are patient facts, so they are always authoritative.
    for (auto& al : p.allergies) {
      if (allergyMatches(al, m))
        add("CONTRAINDICATED", "ALLERGY", m.name + ": patient has a recorded allergy to \"" + al + "\"", {m.index}, true);
    }
    bool pregnancyRuleSeen = false;
    if (m.rules.is_array()) {
      for (auto& r : m.rules) {
        std::string tag = r.value("tag", ""), level = r.value("level", "CAUTION"), note = r.value("note", "");
        if (tag == "PREGNANCY" && level == "CONTRAINDICATED") pregnancyRuleSeen = true;
        if (!tagApplies(tag, p)) continue;
        std::string lvl = level == "CONTRAINDICATED" ? (m.reviewed ? "CONTRAINDICATED" : "WARNING") : "WARNING";
        std::string what = tag == "PREGNANCY" ? "pregnancy" : tag == "BREASTFEEDING" ? "breastfeeding" : "age";
        std::string msg = m.name + ": " + (level == "CONTRAINDICATED" ? "contraindicated" : "use with caution") + " (" + what + ")" +
                          (note.empty() ? "" : " - " + note);
        if (!m.reviewed) msg += " [draft catalogue information, not yet clinically reviewed]";
        add(lvl, "PATIENT_FACTOR_" + tag, msg, {m.index}, m.reviewed);
      }
    }
    if (pregnancyRuleSeen && femaleOfChildbearingAge && pregnancyUnknown)
      add("INFO", "PREGNANCY_STATUS_UNKNOWN",
          m.name + " should be avoided in pregnancy; pregnancy status has not been recorded", {m.index}, m.reviewed);
    if (m.highRisk)
      add("INFO", "HIGH_ALERT_MEDICINE", m.name + " is a high-alert medicine: verify dose, frequency and monitoring", {m.index},
          m.reviewed);
    if (m.restricted)
      add("WARNING", "RESTRICTED_ANTIMICROBIAL", m.name + " is restricted under the clinic's antimicrobial policy: document the indication",
          {m.index}, true);
    else if (m.aware == "WATCH" || m.aware == "RESERVE")
      add("INFO", "ANTIMICROBIAL_STEWARDSHIP", m.name + " is a WHO AWaRe " + m.aware + " antibiotic: confirm the indication", {m.index},
          m.reviewed);
    if (!m.catalogued)
      add("INFO", "NOT_IN_CATALOGUE", m.name + " was entered as free text: no catalogue safety information is available", {m.index}, true);
  }

  // Therapeutic duplication (same molecule twice)
  for (size_t i = 0; i < meds.size(); ++i)
    for (size_t j = i + 1; j < meds.size(); ++j) {
      std::set<std::string> a(meds[i].ingredients.begin(), meds[i].ingredients.end());
      bool overlap = false;
      for (auto& x : meds[j].ingredients) overlap = overlap || a.count(x);
      if (overlap)
        add("WARNING", "DUPLICATE_THERAPY", meds[i].name + " and " + meds[j].name + " contain the same active ingredient",
            {meds[i].index, meds[j].index}, true);
    }

  // Interactions between ingredients of different items
  for (size_t i = 0; i < meds.size(); ++i)
    for (size_t j = i + 1; j < meds.size(); ++j)
      for (auto& ix : interactions) {
        auto has = [&](const SafetyMed& m, const std::string& code) {
          return std::find(m.ingredients.begin(), m.ingredients.end(), code) != m.ingredients.end() || m.genericCode == code;
        };
        bool hit = (has(meds[i], ix.a) && has(meds[j], ix.b)) || (has(meds[i], ix.b) && has(meds[j], ix.a));
        if (!hit) continue;
        std::string level;
        if (ix.severity == "CONTRAINDICATED") level = ix.reviewed ? "CONTRAINDICATED" : "WARNING";
        else if (ix.severity == "MAJOR") level = "WARNING";
        else level = "INFO";
        std::string msg = "Interaction: " + meds[i].name + " + " + meds[j].name + " - " + ix.description;
        if (!ix.reviewed) msg += " [draft interaction data, not yet clinically reviewed]";
        add(level, "INTERACTION_" + ix.severity, msg, {meds[i].index, meds[j].index}, ix.reviewed, ix.reference);
      }

  // Most serious first
  auto rank = [](const json& a) {
    std::string l = a["level"];
    return l == "CONTRAINDICATED" ? 0 : l == "WARNING" ? 1 : 2;
  };
  std::stable_sort(alerts.begin(), alerts.end(), [&](const json& a, const json& b) { return rank(a) < rank(b); });
  return alerts;
}

bool requiresOverride(const json& alerts) {
  for (auto& a : alerts)
    if (a.value("requiresOverride", false)) return true;
  return false;
}

}  // namespace cf::derm
