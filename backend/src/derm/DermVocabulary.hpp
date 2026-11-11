#pragma once
// Coded vocabulary for the dermatology assessment. The same codes are used by the
// CDS knowledge base (feature tokens such as "LESION:WHEAL") and are mirrored in
// frontend/src/lib/derm.ts. Codes are stable identifiers; labels are display text.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cf::derm {

using Vocab = std::vector<std::pair<std::string, std::string>>;  // code, label

inline const Vocab& problemTypes() {
  static const Vocab v = {{"RASH", "Rash / dermatitis"},          {"ACNE_BREAKOUTS", "Acne / breakouts"},
                          {"PIGMENTATION", "Pigmentation change"},  {"ITCH_ONLY", "Itching without rash"},
                          {"INFECTION", "Suspected skin infection"}, {"HIVES_SWELLING", "Hives / swelling"},
                          {"BLISTERS", "Blisters"},                  {"GROWTH_LESION", "Growth / mole / lump"},
                          {"ULCER_WOUND", "Ulcer / non-healing wound"}, {"HAIR_SCALP", "Hair or scalp problem"},
                          {"NAIL", "Nail problem"},                  {"OTHER", "Other"}};
  return v;
}
inline const Vocab& symptoms() {
  static const Vocab v = {{"ITCHING", "Itching"},         {"NIGHT_ITCH", "Itching worse at night"},
                          {"PAIN", "Pain"},               {"BURNING", "Burning"},
                          {"IRRITATION", "Irritation"},   {"SKIN_PAIN", "Skin tenderness / painful skin"},
                          {"FEVER", "Fever"},             {"BLEEDING", "Bleeding"},
                          {"OOZING", "Oozing / weeping"}, {"SWELLING", "Swelling"},
                          {"LIP_TONGUE_SWELLING", "Lip / tongue swelling"},
                          {"BREATHING_DIFFICULTY", "Breathing difficulty"},
                          {"LESION_CHANGE", "Lesion changing in size / shape / colour"},
                          {"CONTACTS_AFFECTED", "Family / close contacts affected"}};
  return v;
}
inline const Vocab& bodySites() {
  static const Vocab v = {{"SCALP", "Scalp"},           {"FACE", "Face"},
                          {"EYE_PERIOCULAR", "Eye / around the eye"}, {"MUCOSA", "Mouth / genital / eye mucosa"},
                          {"NECK", "Neck"},             {"CHEST", "Chest"},
                          {"BACK", "Back"},             {"ABDOMEN", "Abdomen"},
                          {"WAIST", "Waist / belt line"}, {"AXILLA", "Armpits"},
                          {"ARMS", "Arms"},             {"WRISTS", "Wrists"},
                          {"HANDS", "Hands"},           {"FINGER_WEBS", "Finger webs"},
                          {"NAILS", "Nails"},           {"GROIN", "Groin"},
                          {"BUTTOCKS", "Buttocks"},     {"GENITALS", "Genitals"},
                          {"LEGS", "Legs"},             {"FEET", "Feet"}};
  return v;
}
inline const Vocab& lesionTypes() {
  static const Vocab v = {{"MACULE", "Macule (flat, <1 cm)"}, {"PATCH", "Patch (flat, >1 cm)"},
                          {"PAPULE", "Papule"},               {"PLAQUE", "Plaque"},
                          {"NODULE", "Nodule"},               {"CYST", "Cyst"},
                          {"VESICLE", "Vesicle"},             {"BULLA", "Bulla / large blister"},
                          {"PUSTULE", "Pustule"},             {"WHEAL", "Wheal (hive)"},
                          {"COMEDONE", "Comedone"},           {"SCALE", "Scale"},
                          {"CRUST", "Crust"},                 {"EROSION", "Erosion"},
                          {"ULCER", "Ulcer"},                 {"EXCORIATION", "Excoriation (scratch marks)"},
                          {"LICHENIFICATION", "Lichenification"}, {"BURROW", "Burrow"}};
  return v;
}
inline const Vocab& colours() {
  static const Vocab v = {{"ERYTHEMATOUS", "Red / erythematous"}, {"HYPERPIGMENTED", "Darker than skin"},
                          {"HYPOPIGMENTED", "Lighter than skin"},  {"DEPIGMENTED", "Milk-white (depigmented)"},
                          {"BROWN", "Brown"},                      {"YELLOW", "Yellow / honey-coloured"},
                          {"VIOLACEOUS", "Violaceous"},            {"SKIN_COLOURED", "Skin-coloured"}};
  return v;
}
inline const Vocab& configurations() {
  static const Vocab v = {{"DISCRETE", "Discrete"},     {"ANNULAR", "Annular (ring-shaped)"},
                          {"LINEAR", "Linear"},         {"GROUPED", "Grouped / clustered"},
                          {"DERMATOMAL", "Dermatomal (band on one side)"}, {"CONFLUENT", "Confluent"},
                          {"NUMMULAR", "Nummular (coin-shaped)"}};
  return v;
}
inline const Vocab& distributions() {
  static const Vocab v = {{"LOCALISED", "Localised"},       {"GENERALISED", "Generalised / widespread"},
                          {"SYMMETRICAL", "Symmetrical"},   {"ASYMMETRICAL", "Asymmetrical / one-sided"},
                          {"FLEXURAL", "Flexural"},         {"EXTENSOR", "Extensor surfaces"},
                          {"SEBORRHOEIC", "Seborrhoeic areas"}, {"SUN_EXPOSED", "Sun-exposed areas"},
                          {"ACRAL", "Acral (hands / feet)"}, {"INTERTRIGINOUS", "Skin folds"}};
  return v;
}
inline const Vocab& factors() {
  static const Vocab v = {{"HEAT_SWEAT", "Heat / sweating"},   {"SUN", "Sun exposure"},
                          {"COLD_DRY", "Cold / dry weather"},  {"STRESS", "Stress"},
                          {"NEW_PRODUCT", "New cosmetic / product / jewellery"},
                          {"OCCUPATIONAL", "Work exposure"},   {"FOOD", "Food"},
                          {"MEDICINE", "A medicine"},          {"WATER_SOAP", "Water / soap"},
                          {"SCRATCHING", "Scratching"},        {"MOISTURISER", "Moisturiser"},
                          {"COOLING", "Cool compress / cooling"}, {"PRIOR_MEDICINE", "A medicine used before"},
                          {"NOTHING", "Nothing helps / no clear trigger"}};
  return v;
}

inline bool inVocab(const Vocab& v, const std::string& code) {
  for (auto& [c, _] : v)
    if (c == code) return true;
  return false;
}
inline std::string labelOf(const Vocab& v, const std::string& code) {
  for (auto& [c, l] : v)
    if (c == code) return l;
  return code;
}

// Human-readable label for a CDS feature token ("LESION:WHEAL" -> "Lesion: Wheal (hive)")
inline std::string tokenLabel(const std::string& token) {
  auto pos = token.find(':');
  std::string kind = pos == std::string::npos ? token : token.substr(0, pos);
  std::string val = pos == std::string::npos ? "" : token.substr(pos + 1);
  if (kind == "PROBLEM") return "Problem: " + labelOf(problemTypes(), val);
  if (kind == "SYMPTOM") return "Symptom: " + labelOf(symptoms(), val);
  if (kind == "NO_SYMPTOM") return "No " + labelOf(symptoms(), val);
  if (kind == "SITE") return "Site: " + labelOf(bodySites(), val);
  if (kind == "LESION") return "Lesion: " + labelOf(lesionTypes(), val);
  if (kind == "COLOUR") return "Colour: " + labelOf(colours(), val);
  if (kind == "CONFIG") return "Configuration: " + labelOf(configurations(), val);
  if (kind == "DIST") return "Distribution: " + labelOf(distributions(), val);
  if (kind == "AGGRAVATING") return "Trigger: " + labelOf(factors(), val);
  if (kind == "AGE") return "Age group: " + val;
  if (kind == "HISTORY") return "History: " + val;
  if (token == "SPREADING") return "Spreading";
  if (token == "RECURRING") return "Recurring";
  if (kind == "EXTENT_GE") return "Extent >= " + val + "% body surface";
  if (kind == "DURATION_GE_DAYS") return "Duration >= " + val + " days";
  return token;
}

// Banding used by the CDS age features and by de-identified pattern records.
inline std::string cdsAgeGroup(int age) {
  if (age < 12) return "CHILD";
  if (age < 18) return "ADOLESCENT";
  if (age < 35) return "YOUNG_ADULT";
  if (age < 60) return "ADULT";
  return "OLDER_ADULT";
}
// Coarse bands for pattern comparison / network records (k-anonymity friendly)
inline std::string ageBand(int age) {
  if (age < 12) return "0-11";
  if (age < 18) return "12-17";
  if (age < 40) return "18-39";
  if (age < 60) return "40-59";
  return "60+";
}
inline std::optional<int> durationDays(std::optional<int> value, const std::string& unit) {
  if (!value) return std::nullopt;
  if (unit == "DAYS") return *value;
  if (unit == "WEEKS") return *value * 7;
  if (unit == "MONTHS") return *value * 30;
  if (unit == "YEARS") return *value * 365;
  return std::nullopt;
}
inline std::string durationBand(std::optional<int> days) {
  if (!days) return "";
  if (*days < 14) return "<2w";
  if (*days < 42) return "2-6w";
  if (*days < 180) return "6w-6m";
  return ">6m";
}

}  // namespace cf::derm
