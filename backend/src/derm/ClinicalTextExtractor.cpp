#include "ClinicalTextExtractor.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <regex>
#include <set>

#include "DermVocabulary.hpp"

namespace cf::derm {

using json = nlohmann::json;

namespace {

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------
struct Tok {
  std::string w;     // lower-case, singularised
  std::string raw;   // lower-case as written
  size_t start = 0, end = 0;
  int line = 0, clause = 0;
};

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string stem(const std::string& w) {
  if (w.size() > 4 && w.compare(w.size() - 3, 3, "ies") == 0) return w.substr(0, w.size() - 3) + "y";
  if (w.size() > 4 && (w.compare(w.size() - 3, 3, "hes") == 0 || w.compare(w.size() - 3, 3, "xes") == 0 ||
                       w.compare(w.size() - 3, 3, "ses") == 0))
    return w.substr(0, w.size() - 2);
  if (w.size() > 3 && w.back() == 's' && w[w.size() - 2] != 's' && w[w.size() - 2] != 'u') return w.substr(0, w.size() - 1);
  return w;
}

std::vector<Tok> tokenize(const std::string& text) {
  std::vector<Tok> out;
  int line = 0, clause = 0;
  size_t i = 0;
  while (i < text.size()) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == '\n') { ++line; ++clause; ++i; continue; }
    if (c == '.' || c == ';' || c == ',' || c == ':' || c == '(' || c == ')') { ++clause; ++i; continue; }
    if (c == '?') {
      Tok t; t.w = t.raw = "?"; t.start = i; t.end = i + 1; t.line = line; t.clause = clause;
      out.push_back(t); ++i; continue;
    }
    if (std::isalnum(c) || c == '%') {
      size_t j = i;
      while (j < text.size()) {
        unsigned char d = static_cast<unsigned char>(text[j]);
        bool slashBetweenDigits = d == '/' && j > i && std::isdigit(static_cast<unsigned char>(text[j - 1])) && j + 1 < text.size() &&
                                  std::isdigit(static_cast<unsigned char>(text[j + 1]));
        bool hyphenInWord = (d == '-' || d == '\'') && j > i && j + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[j + 1])) &&
                            std::isalpha(static_cast<unsigned char>(text[j - 1]));
        if (std::isalnum(d) || d == '%' || slashBetweenDigits || hyphenInWord) ++j;
        else break;
      }
      Tok t;
      t.raw = lower(text.substr(i, j - i));
      std::string w = t.raw;
      w.erase(std::remove(w.begin(), w.end(), '\''), w.end());
      std::replace(w.begin(), w.end(), '-', ' ');
      t.start = i; t.end = j; t.line = line; t.clause = clause;
      // hyphenated words become several tokens sharing the span ("ring-shaped")
      size_t p = 0;
      while (p < w.size()) {
        size_t q = w.find(' ', p);
        if (q == std::string::npos) q = w.size();
        Tok part = t;
        part.w = stem(w.substr(p, q - p));
        if (!part.w.empty()) out.push_back(part);
        p = q + 1;
      }
      if (t.raw == "but") ++clause;
      i = j;
      continue;
    }
    ++i;
  }
  return out;
}

// ---------------------------------------------------------------------------
// Lexicon (phrases are tokenised the same way as the note)
// ---------------------------------------------------------------------------
enum class Cat { SYMPTOM, SITE, LESION, COLOUR, CONFIG, DIST, SEVERITY, FACTOR, PROBLEM, SPREADING, RECURRING, PREGNANCY };

struct Entry {
  std::vector<std::string> words;
  Cat cat;
  std::vector<std::string> codes;  // one phrase can imply several codes (e.g. "honey crust")
  std::string phrase;
};

const char* catName(Cat c) {
  switch (c) {
    case Cat::SYMPTOM: return "symptom";
    case Cat::SITE: return "body site";
    case Cat::LESION: return "lesion";
    case Cat::COLOUR: return "colour";
    case Cat::CONFIG: return "configuration";
    case Cat::DIST: return "distribution";
    case Cat::SEVERITY: return "severity";
    case Cat::FACTOR: return "trigger";
    case Cat::PROBLEM: return "problem";
    case Cat::SPREADING: return "course";
    case Cat::RECURRING: return "course";
    case Cat::PREGNANCY: return "pregnancy";
  }
  return "";
}

std::vector<std::string> words(const std::string& phrase) {
  std::vector<std::string> w;
  for (auto& t : tokenize(phrase)) w.push_back(t.w);
  return w;
}

const std::vector<Entry>& lexicon() {
  static const std::vector<Entry> lex = [] {
    std::vector<std::pair<std::string, std::pair<Cat, std::vector<std::string>>>> raw = {
        // symptoms
        {"itch", {Cat::SYMPTOM, {"ITCHING"}}}, {"itching", {Cat::SYMPTOM, {"ITCHING"}}}, {"itchy", {Cat::SYMPTOM, {"ITCHING"}}},
        {"pruritus", {Cat::SYMPTOM, {"ITCHING"}}}, {"pruritic", {Cat::SYMPTOM, {"ITCHING"}}}, {"itchiness", {Cat::SYMPTOM, {"ITCHING"}}},
        {"worse at night", {Cat::SYMPTOM, {"NIGHT_ITCH"}}}, {"nocturnal", {Cat::SYMPTOM, {"NIGHT_ITCH"}}}, {"night itch", {Cat::SYMPTOM, {"NIGHT_ITCH"}}},
        {"itching at night", {Cat::SYMPTOM, {"NIGHT_ITCH", "ITCHING"}}},
        {"pain", {Cat::SYMPTOM, {"PAIN"}}}, {"painful", {Cat::SYMPTOM, {"PAIN"}}}, {"burning", {Cat::SYMPTOM, {"BURNING"}}},
        {"irritation", {Cat::SYMPTOM, {"IRRITATION"}}}, {"tender", {Cat::SYMPTOM, {"SKIN_PAIN"}}}, {"tenderness", {Cat::SYMPTOM, {"SKIN_PAIN"}}},
        {"skin pain", {Cat::SYMPTOM, {"SKIN_PAIN"}}}, {"fever", {Cat::SYMPTOM, {"FEVER"}}}, {"febrile", {Cat::SYMPTOM, {"FEVER"}}},
        {"bleeding", {Cat::SYMPTOM, {"BLEEDING"}}}, {"bleed", {Cat::SYMPTOM, {"BLEEDING"}}}, {"oozing", {Cat::SYMPTOM, {"OOZING"}}},
        {"weeping", {Cat::SYMPTOM, {"OOZING"}}}, {"discharge", {Cat::SYMPTOM, {"OOZING"}}}, {"swelling", {Cat::SYMPTOM, {"SWELLING"}}},
        {"swollen", {Cat::SYMPTOM, {"SWELLING"}}}, {"oedema", {Cat::SYMPTOM, {"SWELLING"}}}, {"edema", {Cat::SYMPTOM, {"SWELLING"}}},
        {"lip swelling", {Cat::SYMPTOM, {"LIP_TONGUE_SWELLING"}}}, {"tongue swelling", {Cat::SYMPTOM, {"LIP_TONGUE_SWELLING"}}},
        {"swelling of lip", {Cat::SYMPTOM, {"LIP_TONGUE_SWELLING"}}}, {"angioedema", {Cat::SYMPTOM, {"LIP_TONGUE_SWELLING"}}},
        {"breathlessness", {Cat::SYMPTOM, {"BREATHING_DIFFICULTY"}}}, {"difficulty breathing", {Cat::SYMPTOM, {"BREATHING_DIFFICULTY"}}},
        {"difficulty in breathing", {Cat::SYMPTOM, {"BREATHING_DIFFICULTY"}}}, {"wheeze", {Cat::SYMPTOM, {"BREATHING_DIFFICULTY"}}},
        {"sob", {Cat::SYMPTOM, {"BREATHING_DIFFICULTY"}}}, {"shortness of breath", {Cat::SYMPTOM, {"BREATHING_DIFFICULTY"}}},
        {"increasing in size", {Cat::SYMPTOM, {"LESION_CHANGE"}}}, {"change in size", {Cat::SYMPTOM, {"LESION_CHANGE"}}},
        {"change in colour", {Cat::SYMPTOM, {"LESION_CHANGE"}}}, {"changing", {Cat::SYMPTOM, {"LESION_CHANGE"}}},
        {"family member", {Cat::SYMPTOM, {"CONTACTS_AFFECTED"}}}, {"household contact", {Cat::SYMPTOM, {"CONTACTS_AFFECTED"}}},
        {"contacts affected", {Cat::SYMPTOM, {"CONTACTS_AFFECTED"}}}, {"family also", {Cat::SYMPTOM, {"CONTACTS_AFFECTED"}}},
        {"sibling", {Cat::SYMPTOM, {"CONTACTS_AFFECTED"}}},
        // body sites
        {"scalp", {Cat::SITE, {"SCALP"}}}, {"face", {Cat::SITE, {"FACE"}}}, {"facial", {Cat::SITE, {"FACE"}}}, {"cheek", {Cat::SITE, {"FACE"}}},
        {"forehead", {Cat::SITE, {"FACE"}}}, {"chin", {Cat::SITE, {"FACE"}}}, {"nose", {Cat::SITE, {"FACE"}}}, {"jaw", {Cat::SITE, {"FACE"}}},
        {"malar", {Cat::SITE, {"FACE"}}}, {"eye", {Cat::SITE, {"EYE_PERIOCULAR"}}}, {"eyelid", {Cat::SITE, {"EYE_PERIOCULAR"}}},
        {"periorbital", {Cat::SITE, {"EYE_PERIOCULAR"}}}, {"periocular", {Cat::SITE, {"EYE_PERIOCULAR"}}}, {"mouth", {Cat::SITE, {"MUCOSA"}}},
        {"oral", {Cat::SITE, {"MUCOSA"}}}, {"lip", {Cat::SITE, {"MUCOSA"}}}, {"mucosa", {Cat::SITE, {"MUCOSA"}}}, {"mucosal", {Cat::SITE, {"MUCOSA"}}},
        {"neck", {Cat::SITE, {"NECK"}}}, {"chest", {Cat::SITE, {"CHEST"}}}, {"back", {Cat::SITE, {"BACK"}}}, {"trunk", {Cat::SITE, {"CHEST", "BACK"}}},
        {"abdomen", {Cat::SITE, {"ABDOMEN"}}}, {"tummy", {Cat::SITE, {"ABDOMEN"}}}, {"belly", {Cat::SITE, {"ABDOMEN"}}},
        {"waist", {Cat::SITE, {"WAIST"}}}, {"belt line", {Cat::SITE, {"WAIST"}}}, {"axilla", {Cat::SITE, {"AXILLA"}}}, {"armpit", {Cat::SITE, {"AXILLA"}}},
        {"arm", {Cat::SITE, {"ARMS"}}}, {"forearm", {Cat::SITE, {"ARMS"}}}, {"elbow", {Cat::SITE, {"ARMS"}}}, {"upper limb", {Cat::SITE, {"ARMS"}}},
        {"wrist", {Cat::SITE, {"WRISTS"}}}, {"hand", {Cat::SITE, {"HANDS"}}}, {"palm", {Cat::SITE, {"HANDS"}}},
        {"finger web", {Cat::SITE, {"FINGER_WEBS"}}}, {"web space", {Cat::SITE, {"FINGER_WEBS"}}}, {"interdigital", {Cat::SITE, {"FINGER_WEBS"}}},
        {"nail", {Cat::SITE, {"NAILS"}}}, {"groin", {Cat::SITE, {"GROIN"}}}, {"inguinal", {Cat::SITE, {"GROIN"}}}, {"crural", {Cat::SITE, {"GROIN"}}},
        {"buttock", {Cat::SITE, {"BUTTOCKS"}}}, {"gluteal", {Cat::SITE, {"BUTTOCKS"}}}, {"genital", {Cat::SITE, {"GENITALS"}}},
        {"genitalia", {Cat::SITE, {"GENITALS"}}}, {"scrotum", {Cat::SITE, {"GENITALS"}}}, {"leg", {Cat::SITE, {"LEGS"}}}, {"thigh", {Cat::SITE, {"LEGS"}}},
        {"knee", {Cat::SITE, {"LEGS"}}}, {"shin", {Cat::SITE, {"LEGS"}}}, {"lower limb", {Cat::SITE, {"LEGS"}}}, {"foot", {Cat::SITE, {"FEET"}}},
        {"feet", {Cat::SITE, {"FEET"}}}, {"sole", {Cat::SITE, {"FEET"}}}, {"toe", {Cat::SITE, {"FEET"}}},
        // lesions
        {"macule", {Cat::LESION, {"MACULE"}}}, {"macular", {Cat::LESION, {"MACULE"}}}, {"patch", {Cat::LESION, {"PATCH"}}},
        {"papule", {Cat::LESION, {"PAPULE"}}}, {"papular", {Cat::LESION, {"PAPULE"}}}, {"bump", {Cat::LESION, {"PAPULE"}}},
        {"plaque", {Cat::LESION, {"PLAQUE"}}}, {"nodule", {Cat::LESION, {"NODULE"}}}, {"nodular", {Cat::LESION, {"NODULE"}}},
        {"cyst", {Cat::LESION, {"CYST"}}}, {"cystic", {Cat::LESION, {"CYST"}}}, {"vesicle", {Cat::LESION, {"VESICLE"}}},
        {"vesicular", {Cat::LESION, {"VESICLE"}}}, {"blister", {Cat::LESION, {"VESICLE"}}}, {"bulla", {Cat::LESION, {"BULLA"}}},
        {"bullae", {Cat::LESION, {"BULLA"}}}, {"bullous", {Cat::LESION, {"BULLA"}}}, {"large blister", {Cat::LESION, {"BULLA"}}},
        {"pustule", {Cat::LESION, {"PUSTULE"}}}, {"pustular", {Cat::LESION, {"PUSTULE"}}}, {"pus filled", {Cat::LESION, {"PUSTULE"}}},
        {"wheal", {Cat::LESION, {"WHEAL"}}}, {"weal", {Cat::LESION, {"WHEAL"}}}, {"hive", {Cat::LESION, {"WHEAL"}}},
        {"comedone", {Cat::LESION, {"COMEDONE"}}}, {"comedonal", {Cat::LESION, {"COMEDONE"}}}, {"blackhead", {Cat::LESION, {"COMEDONE"}}},
        {"whitehead", {Cat::LESION, {"COMEDONE"}}}, {"scale", {Cat::LESION, {"SCALE"}}}, {"scaly", {Cat::LESION, {"SCALE"}}},
        {"scaling", {Cat::LESION, {"SCALE"}}}, {"flaky", {Cat::LESION, {"SCALE"}}}, {"flaking", {Cat::LESION, {"SCALE"}}},
        {"crust", {Cat::LESION, {"CRUST"}}}, {"crusted", {Cat::LESION, {"CRUST"}}}, {"crusting", {Cat::LESION, {"CRUST"}}},
        {"honey crust", {Cat::LESION, {"CRUST", "YELLOW"}}}, {"honey coloured crust", {Cat::LESION, {"CRUST", "YELLOW"}}},
        {"erosion", {Cat::LESION, {"EROSION"}}}, {"eroded", {Cat::LESION, {"EROSION"}}}, {"ulcer", {Cat::LESION, {"ULCER"}}},
        {"ulceration", {Cat::LESION, {"ULCER"}}}, {"excoriation", {Cat::LESION, {"EXCORIATION"}}}, {"excoriated", {Cat::LESION, {"EXCORIATION"}}},
        {"scratch mark", {Cat::LESION, {"EXCORIATION"}}}, {"lichenification", {Cat::LESION, {"LICHENIFICATION"}}},
        {"lichenified", {Cat::LESION, {"LICHENIFICATION"}}}, {"thickened skin", {Cat::LESION, {"LICHENIFICATION"}}},
        {"burrow", {Cat::LESION, {"BURROW"}}},
        // colours
        {"red", {Cat::COLOUR, {"ERYTHEMATOUS"}}}, {"erythematous", {Cat::COLOUR, {"ERYTHEMATOUS"}}}, {"erythema", {Cat::COLOUR, {"ERYTHEMATOUS"}}},
        {"hyperpigmented", {Cat::COLOUR, {"HYPERPIGMENTED"}}}, {"hyperpigmentation", {Cat::COLOUR, {"HYPERPIGMENTED"}}},
        {"dark", {Cat::COLOUR, {"HYPERPIGMENTED"}}}, {"brown", {Cat::COLOUR, {"BROWN", "HYPERPIGMENTED"}}},
        {"hypopigmented", {Cat::COLOUR, {"HYPOPIGMENTED"}}}, {"hypopigmentation", {Cat::COLOUR, {"HYPOPIGMENTED"}}},
        {"depigmented", {Cat::COLOUR, {"DEPIGMENTED"}}}, {"depigmentation", {Cat::COLOUR, {"DEPIGMENTED"}}},
        {"milky white", {Cat::COLOUR, {"DEPIGMENTED"}}}, {"chalk white", {Cat::COLOUR, {"DEPIGMENTED"}}},
        {"yellow", {Cat::COLOUR, {"YELLOW"}}}, {"yellowish", {Cat::COLOUR, {"YELLOW"}}}, {"greasy", {Cat::COLOUR, {"YELLOW"}}},
        {"violaceous", {Cat::COLOUR, {"VIOLACEOUS"}}}, {"purple", {Cat::COLOUR, {"VIOLACEOUS"}}},
        {"skin coloured", {Cat::COLOUR, {"SKIN_COLOURED"}}}, {"skin colored", {Cat::COLOUR, {"SKIN_COLOURED"}}},
        // configuration
        {"annular", {Cat::CONFIG, {"ANNULAR"}}}, {"ring shaped", {Cat::CONFIG, {"ANNULAR"}}}, {"ring like", {Cat::CONFIG, {"ANNULAR"}}},
        {"circular", {Cat::CONFIG, {"ANNULAR"}}}, {"linear", {Cat::CONFIG, {"LINEAR"}}}, {"grouped", {Cat::CONFIG, {"GROUPED"}}},
        {"clustered", {Cat::CONFIG, {"GROUPED"}}}, {"dermatomal", {Cat::CONFIG, {"DERMATOMAL"}}}, {"band like", {Cat::CONFIG, {"DERMATOMAL"}}},
        {"confluent", {Cat::CONFIG, {"CONFLUENT"}}}, {"nummular", {Cat::CONFIG, {"NUMMULAR"}}}, {"coin shaped", {Cat::CONFIG, {"NUMMULAR"}}},
        {"discrete", {Cat::CONFIG, {"DISCRETE"}}},
        // distribution
        {"generalised", {Cat::DIST, {"GENERALISED"}}}, {"generalized", {Cat::DIST, {"GENERALISED"}}}, {"widespread", {Cat::DIST, {"GENERALISED"}}},
        {"all over", {Cat::DIST, {"GENERALISED"}}}, {"whole body", {Cat::DIST, {"GENERALISED"}}}, {"localised", {Cat::DIST, {"LOCALISED"}}},
        {"localized", {Cat::DIST, {"LOCALISED"}}}, {"symmetrical", {Cat::DIST, {"SYMMETRICAL"}}}, {"symmetric", {Cat::DIST, {"SYMMETRICAL"}}},
        {"bilateral", {Cat::DIST, {"SYMMETRICAL"}}}, {"unilateral", {Cat::DIST, {"ASYMMETRICAL"}}}, {"one side", {Cat::DIST, {"ASYMMETRICAL"}}},
        {"flexural", {Cat::DIST, {"FLEXURAL"}}}, {"flexure", {Cat::DIST, {"FLEXURAL"}}}, {"extensor", {Cat::DIST, {"EXTENSOR"}}},
        {"sun exposed", {Cat::DIST, {"SUN_EXPOSED"}}}, {"photo exposed", {Cat::DIST, {"SUN_EXPOSED"}}},
        {"seborrhoeic area", {Cat::DIST, {"SEBORRHOEIC"}}}, {"seborrheic area", {Cat::DIST, {"SEBORRHOEIC"}}}, {"acral", {Cat::DIST, {"ACRAL"}}},
        {"intertriginous", {Cat::DIST, {"INTERTRIGINOUS"}}}, {"skin fold", {Cat::DIST, {"INTERTRIGINOUS"}}},
        // severity
        {"mild", {Cat::SEVERITY, {"MILD"}}}, {"moderate", {Cat::SEVERITY, {"MODERATE"}}}, {"mod", {Cat::SEVERITY, {"MODERATE"}}},
        {"severe", {Cat::SEVERITY, {"SEVERE"}}}, {"sev", {Cat::SEVERITY, {"SEVERE"}}},
        // triggers (only taken from clauses with an aggravation cue)
        {"heat", {Cat::FACTOR, {"HEAT_SWEAT"}}}, {"sweat", {Cat::FACTOR, {"HEAT_SWEAT"}}}, {"sweating", {Cat::FACTOR, {"HEAT_SWEAT"}}},
        {"sun", {Cat::FACTOR, {"SUN"}}}, {"sunlight", {Cat::FACTOR, {"SUN"}}}, {"cold", {Cat::FACTOR, {"COLD_DRY"}}}, {"winter", {Cat::FACTOR, {"COLD_DRY"}}},
        {"stress", {Cat::FACTOR, {"STRESS"}}}, {"new cream", {Cat::FACTOR, {"NEW_PRODUCT"}}}, {"cosmetic", {Cat::FACTOR, {"NEW_PRODUCT"}}},
        {"detergent", {Cat::FACTOR, {"NEW_PRODUCT"}}}, {"jewellery", {Cat::FACTOR, {"NEW_PRODUCT"}}}, {"hair dye", {Cat::FACTOR, {"NEW_PRODUCT"}}},
        {"new soap", {Cat::FACTOR, {"NEW_PRODUCT"}}}, {"work", {Cat::FACTOR, {"OCCUPATIONAL"}}}, {"occupational", {Cat::FACTOR, {"OCCUPATIONAL"}}},
        {"food", {Cat::FACTOR, {"FOOD"}}}, {"scratching", {Cat::FACTOR, {"SCRATCHING"}}},
        // problem hints
        {"hives", {Cat::PROBLEM, {"HIVES_SWELLING"}}}, {"acne", {Cat::PROBLEM, {"ACNE_BREAKOUTS"}}}, {"pimple", {Cat::PROBLEM, {"ACNE_BREAKOUTS"}}},
        {"zit", {Cat::PROBLEM, {"ACNE_BREAKOUTS"}}}, {"rash", {Cat::PROBLEM, {"RASH"}}}, {"rashes", {Cat::PROBLEM, {"RASH"}}},
        {"mole", {Cat::PROBLEM, {"GROWTH_LESION"}}}, {"growth", {Cat::PROBLEM, {"GROWTH_LESION"}}}, {"lump", {Cat::PROBLEM, {"GROWTH_LESION"}}},
        {"hair fall", {Cat::PROBLEM, {"HAIR_SCALP"}}}, {"hair loss", {Cat::PROBLEM, {"HAIR_SCALP"}}}, {"dandruff", {Cat::PROBLEM, {"HAIR_SCALP"}}},
        {"wound", {Cat::PROBLEM, {"ULCER_WOUND"}}}, {"pigmentation", {Cat::PROBLEM, {"PIGMENTATION"}}}, {"dark spot", {Cat::PROBLEM, {"PIGMENTATION"}}},
        {"white patch", {Cat::PROBLEM, {"PIGMENTATION"}}},
        // course
        {"spreading", {Cat::SPREADING, {"TRUE"}}}, {"progressive", {Cat::SPREADING, {"TRUE"}}}, {"increasing", {Cat::SPREADING, {"TRUE"}}},
        {"recurrent", {Cat::RECURRING, {"TRUE"}}}, {"recurring", {Cat::RECURRING, {"TRUE"}}}, {"recurrence", {Cat::RECURRING, {"TRUE"}}},
        {"on and off", {Cat::RECURRING, {"TRUE"}}}, {"relapsing", {Cat::RECURRING, {"TRUE"}}}, {"episodic", {Cat::RECURRING, {"TRUE"}}},
        // pregnancy
        {"pregnant", {Cat::PREGNANCY, {"PREGNANT"}}}, {"pregnancy", {Cat::PREGNANCY, {"PREGNANT"}}}, {"breastfeeding", {Cat::PREGNANCY, {"BREASTFEEDING"}}},
        {"lactating", {Cat::PREGNANCY, {"BREASTFEEDING"}}}, {"lactation", {Cat::PREGNANCY, {"BREASTFEEDING"}}},
    };
    std::vector<Entry> v;
    for (auto& [p, cc] : raw) v.push_back({words(p), cc.first, cc.second, p});
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.words.size() > b.words.size(); });
    return v;
  }();
  return lex;
}

// Common words that must never be "corrected" into a medical term
const std::set<std::string>& commonWords() {
  static const std::set<std::string> s = {"there", "these", "those", "where", "which", "while", "about", "after", "again", "since",
                                          "other", "under", "taken", "given", "using", "being", "times", "daily", "night", "weeks",
                                          "month", "years", "today", "patient", "advised", "review", "applied", "started", "noted",
                                          "small", "large", "multiple", "single", "slight", "black", "white", "light", "lesion",
                                          "areas", "around", "spread", "present", "absent", "change", "bathe", "bath", "check",
                                          "follow", "father", "mother", "brother", "sister", "child", "weight", "height", "water",
                                          "cream", "treat", "start", "stop", "itself", "before", "night", "side", "place", "plan",
                                          "rash", "mild", "pain", "back", "face", "cold", "heat", "work", "food", "nail", "hand",
                                          "neck", "nose", "chin", "palm", "sole", "toe", "eye", "lip", "arm", "leg", "fever"};
  return s;
}

bool isNegator(const std::string& w) {
  static const std::set<std::string> n = {"no", "nil", "not", "deny", "denie", "denied", "without", "absent", "negative", "neg", "never", "none"};
  return n.count(w) > 0;
}

std::string titleCase(std::string s) {
  bool up = true;
  for (auto& c : s) {
    if (up && std::isalpha(static_cast<unsigned char>(c))) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); up = false; }
    else if (c == ' ') up = true;
  }
  return s;
}

int numberWord(const std::string& w) {
  static const std::map<std::string, int> m = {{"a", 1}, {"an", 1}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5},
                                               {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}, {"eleven", 11},
                                               {"twelve", 12}, {"few", 3}, {"couple", 2}};
  auto it = m.find(w);
  return it == m.end() ? -1 : it->second;
}

}  // namespace

int editDistance(const std::string& a, const std::string& b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

const std::vector<KnownDiagnosis>& dermDiagnosisLexicon() {
  static const std::vector<KnownDiagnosis> d = {
      {"L50.9", "Urticaria, unspecified", {"urticaria", "hives", "chronic urticaria", "acute urticaria"}},
      {"B35.4", "Tinea corporis", {"tinea corporis", "ringworm", "tinea", "dermatophytosis", "fungal infection"}},
      {"B35.6", "Tinea cruris", {"tinea cruris", "jock itch"}},
      {"B35.3", "Tinea pedis", {"tinea pedis", "athlete foot"}},
      {"B36.0", "Pityriasis versicolor", {"pityriasis versicolor", "tinea versicolor", "pv"}},
      {"L70.0", "Acne vulgaris", {"acne vulgaris", "acne"}},
      {"L20.9", "Atopic dermatitis, unspecified", {"atopic dermatitis", "atopic eczema", "eczema", "ad"}},
      {"L40.0", "Psoriasis vulgaris", {"psoriasis vulgaris", "psoriasis", "plaque psoriasis"}},
      {"L21.9", "Seborrhoeic dermatitis, unspecified", {"seborrhoeic dermatitis", "seborrheic dermatitis", "seb derm"}},
      {"B86", "Scabies", {"scabies"}},
      {"L81.1", "Chloasma", {"melasma", "chloasma"}},
      {"L80", "Vitiligo", {"vitiligo", "leucoderma", "leukoderma"}},
      {"L25.9", "Unspecified contact dermatitis", {"contact dermatitis", "allergic contact dermatitis", "irritant contact dermatitis"}},
      {"B02.9", "Zoster without complications", {"herpes zoster", "zoster", "shingles"}},
      {"L01.0", "Impetigo", {"impetigo"}},
  };
  return d;
}

json extractClinicalText(const std::string& text, const std::vector<KnownMedicine>& medicines, const std::vector<KnownDiagnosis>& diagnoses) {
  auto toks = tokenize(text);
  auto evidence = [&](size_t a, size_t b) { return text.substr(toks[a].start, toks[b].end - toks[a].start); };

  // ---- negation scopes: a negator covers the rest of its clause, up to 5 words
  std::vector<bool> negated(toks.size(), false);
  for (size_t i = 0; i < toks.size(); ++i) {
    if (!isNegator(toks[i].w)) continue;
    for (size_t j = i + 1; j < toks.size() && j <= i + 5 && toks[j].clause == toks[i].clause; ++j) negated[j] = true;
  }
  // "-ve" written after a finding ("fever -ve")
  for (size_t i = 1; i < toks.size(); ++i)
    if (toks[i].w == "ve" && toks[i].start > 0 && text[toks[i].start - 1] == '-') negated[i - 1] = true;

  // ---- sections from line headings
  std::vector<std::string> lines;
  {
    size_t p = 0;
    while (p <= text.size()) {
      size_t q = text.find('\n', p);
      if (q == std::string::npos) q = text.size();
      lines.push_back(text.substr(p, q - p));
      p = q + 1;
    }
  }
  static const std::vector<std::pair<std::regex, std::string>> heads = {
      {std::regex(R"(^\s*(c\/o|cc|chief complaints?|complaints?|presenting complaints?)\b\s*[:\-]?\s*)", std::regex::icase), "complaint"},
      {std::regex(R"(^\s*(h\/o|hx|hpi|history( of present illness)?)\b\s*[:\-]?\s*)", std::regex::icase), "history"},
      {std::regex(R"(^\s*(o\/e|oe|on examination|examination|exam|findings|clinical findings|cutaneous examination)\b\s*[:\-]?\s*)", std::regex::icase), "examination"},
      {std::regex(R"(^\s*(imp(ression)?|dx|diagnosis|provisional diagnosis|ddx|d\/d|differentials?)\b\s*[:\-]?\s*)", std::regex::icase), "diagnosis"},
      {std::regex(R"(^\s*(rx|plan|treatment|tx|advice|advised|management)\b\s*[:\-]?\s*)", std::regex::icase), "plan"},
  };
  std::map<std::string, std::string> sections;
  std::vector<std::string> lineSection(lines.size(), "");
  std::string current;
  bool anyHeading = false;
  for (size_t li = 0; li < lines.size(); ++li) {
    std::string body = lines[li];
    for (auto& [re, name] : heads) {
      std::smatch m;
      if (std::regex_search(body, m, re)) {
        current = name;
        anyHeading = true;
        body = m.suffix();
        break;
      }
    }
    std::string trimmed = body;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r") + 1 == 0 ? 0 : trimmed.find_last_not_of(" \t\r") + 1);
    std::string sec = current.empty() ? "other" : current;
    lineSection[li] = sec;
    if (trimmed.empty()) continue;
    auto& s = sections[sec];
    s += (s.empty() ? "" : "\n") + trimmed;
  }
  if (!anyHeading && !lines.empty()) {
    // No headings: the first non-empty line is taken as the complaint, the rest as notes.
    std::string first, rest;
    for (auto& l : lines) {
      std::string t = l;
      t.erase(0, t.find_first_not_of(" \t\r"));
      if (t.empty()) continue;
      if (first.empty()) first = t;
      else rest += (rest.empty() ? "" : "\n") + t;
    }
    sections.clear();
    if (!first.empty()) sections["complaint"] = first;
    if (!rest.empty()) sections["other"] = rest;
  }

  // ---- medicines (matched first so their words are not read as findings)
  std::vector<bool> used(toks.size(), false);
  json meds = json::array();
  json corrections = json::array();
  std::set<std::string> correctionSeen;
  auto addCorrection = [&](const std::string& original, const std::string& suggestion, const std::string& cat) {
    if (lower(original) == lower(suggestion) || correctionSeen.count(lower(original))) return;
    correctionSeen.insert(lower(original));
    corrections.push_back({{"original", original}, {"suggestion", suggestion}, {"category", cat}});
  };
  struct MedAlias {
    std::vector<std::string> w;
    const KnownMedicine* m;
  };
  std::vector<MedAlias> aliases;
  for (auto& m : medicines) {
    auto w = words(m.genericName);
    if (w.empty()) continue;
    aliases.push_back({w, &m});
    if (w.size() > 1 && w[0].size() >= 6) aliases.push_back({{w[0]}, &m});
  }
  std::sort(aliases.begin(), aliases.end(), [](const MedAlias& a, const MedAlias& b) { return a.w.size() > b.w.size(); });
  static const std::regex strengthRe(R"(\b(\d+(?:\.\d+)?)\s*(mg|mcg|g|gm|ml|%|iu)\b)", std::regex::icase);
  static const std::vector<std::pair<std::regex, std::string>> freqs = {
      {std::regex(R"(\b(od|once daily|once a day|qd|daily)\b)", std::regex::icase), "Once daily"},
      {std::regex(R"(\b(bd|bid|twice daily|twice a day)\b)", std::regex::icase), "Twice daily"},
      {std::regex(R"(\b(tds|tid|thrice daily|three times daily|three times a day)\b)", std::regex::icase), "Three times daily"},
      {std::regex(R"(\b(qid|qds|four times daily)\b)", std::regex::icase), "Four times daily"},
      {std::regex(R"(\b(hs|at night|at bedtime|bedtime|nocte)\b)", std::regex::icase), "At night"},
      {std::regex(R"(\b(sos|prn|as needed|when required)\b)", std::regex::icase), "When required"},
      {std::regex(R"(\b(stat|single dose)\b)", std::regex::icase), "Single dose"},
      {std::regex(R"(\b(once weekly|weekly)\b)", std::regex::icase), "Once weekly"},
      {std::regex(R"(\b(1-0-1)\b)"), "Twice daily (1-0-1)"},
      {std::regex(R"(\b(1-1-1)\b)"), "Three times daily (1-1-1)"},
      {std::regex(R"(\b(0-0-1)\b)"), "At night (0-0-1)"},
      {std::regex(R"(\b(1-0-0)\b)"), "Once daily, morning (1-0-0)"},
  };
  static const std::regex medDurRe(R"((?:x|for|×)\s*(\d+)\s*(days?|d|weeks?|wks?|w|months?|mo|m)\b|\b(\d+)\/(7|52|12)\b)", std::regex::icase);
  std::vector<std::pair<int, size_t>> medSpans;  // line, char offset where a medicine mention starts
  for (size_t i = 0; i < toks.size(); ++i) {
    if (used[i]) continue;
    for (auto& al : aliases) {
      size_t n = al.w.size();
      if (i + n > toks.size()) continue;
      bool exact = true, fuzzy = false;
      for (size_t k = 0; k < n && exact; ++k) {
        if (toks[i + k].line != toks[i].line) { exact = false; break; }
        if (toks[i + k].w == al.w[k]) continue;
        int d = editDistance(toks[i + k].w, al.w[k]);
        if (n == 1 && al.w[k].size() >= 7 && d <= (al.w[k].size() >= 10 ? 2 : 1) && !commonWords().count(toks[i + k].w)) { fuzzy = true; continue; }
        exact = false;
      }
      if (!exact) continue;
      for (size_t k = 0; k < n; ++k) used[i + k] = true;
      size_t endTok = toks[i + n - 1].end;
      size_t eol = text.find('\n', endTok);
      if (eol == std::string::npos) eol = text.size();
      std::string after = text.substr(endTok, eol - endTok);
      json m = {{"genericCode", al.m->genericCode}, {"name", al.m->genericName}, {"evidence", evidence(i, i + n - 1) + after},
                {"fuzzy", fuzzy}, {"negated", negated[i]}};
      std::smatch sm;
      if (std::regex_search(after, sm, strengthRe)) m["strength"] = sm.str(1) + " " + lower(sm.str(2)) ;
      for (auto& [re, label] : freqs)
        if (std::regex_search(after, sm, re)) { m["frequency"] = label; break; }
      if (std::regex_search(after, sm, medDurRe)) {
        int v = 0, mult = 1;
        if (sm[1].matched) {
          v = std::stoi(sm.str(1));
          char u = static_cast<char>(std::tolower(static_cast<unsigned char>(sm.str(2)[0])));
          mult = u == 'w' ? 7 : u == 'm' ? 30 : 1;
        } else {
          v = std::stoi(sm.str(3));
          mult = sm.str(4) == "52" ? 7 : sm.str(4) == "12" ? 30 : 1;
        }
        if (v > 0 && v * mult <= 365) m["durationDays"] = v * mult;
      }
      if (fuzzy) addCorrection(evidence(i, i), al.m->genericName, "medicine");
      if (!negated[i]) meds.push_back(m);
      medSpans.push_back({toks[i].line, toks[i].start});
      break;
    }
  }

  // ---- diagnoses written by the doctor
  json dx = json::array(), dxExcluded = json::array();
  std::set<std::string> dxSeen;
  struct DxAlias {
    std::vector<std::string> w;
    const KnownDiagnosis* d;
  };
  std::vector<DxAlias> dxa;
  for (auto& d : diagnoses)
    for (auto& s : d.synonyms) dxa.push_back({words(s), &d});
  std::sort(dxa.begin(), dxa.end(), [](const DxAlias& a, const DxAlias& b) { return a.w.size() > b.w.size(); });
  for (size_t i = 0; i < toks.size(); ++i) {
    if (used[i]) continue;
    for (auto& a : dxa) {
      size_t n = a.w.size();
      if (i + n > toks.size()) continue;
      // very short abbreviations (ad, pv) only count in the diagnosis section
      if (n == 1 && a.w[0].size() <= 2 && lineSection[toks[i].line] != "diagnosis") continue;
      bool ok = true, fuzzy = false;
      for (size_t k = 0; k < n && ok; ++k) {
        if (toks[i + k].w == a.w[k]) continue;
        int d = editDistance(toks[i + k].w, a.w[k]);
        if (a.w[k].size() >= 6 && d <= (a.w[k].size() >= 10 ? 2 : 1) && !commonWords().count(toks[i + k].w)) { fuzzy = true; continue; }
        ok = false;
      }
      if (!ok) continue;
      for (size_t k = 0; k < n; ++k) used[i + k] = true;
      bool query = (i > 0 && toks[i - 1].w == "?") || lineSection[toks[i].line] == "diagnosis";
      for (size_t k = (i >= 3 ? i - 3 : 0); k < i; ++k)
        if (toks[k].clause == toks[i].clause && (toks[k].w == "r" || toks[k].w == "ddx" || toks[k].w == "likely" || toks[k].w == "possible" ||
                                                 toks[k].w == "probable" || toks[k].w == "suspected" || toks[k].w == "query"))
          query = true;
      if (fuzzy) addCorrection(evidence(i, i + n - 1), a.w.size() > 1 ? titleCase(a.d->synonyms[0]) : titleCase(a.w[0]), "diagnosis");
      json item = {{"code", a.d->code}, {"display", a.d->display}, {"evidence", evidence(i, i + n - 1)}, {"fuzzy", fuzzy},
                   {"writtenAs", query ? "query" : "stated"}};
      if (negated[i]) {
        dxExcluded.push_back(item);
      } else if (!dxSeen.count(a.d->code)) {
        dxSeen.insert(a.d->code);
        dx.push_back(item);
      }
      break;
    }
  }

  // ---- findings
  std::map<Cat, json> found;
  for (auto c : {Cat::SYMPTOM, Cat::SITE, Cat::LESION, Cat::COLOUR, Cat::CONFIG, Cat::DIST, Cat::SEVERITY, Cat::FACTOR, Cat::PROBLEM,
                 Cat::SPREADING, Cat::RECURRING, Cat::PREGNANCY})
    found[c] = json::array();
  json negatives = json::array();
  std::map<Cat, std::set<std::string>> seen;
  std::set<int> aggravationClauses;
  for (size_t i = 0; i < toks.size(); ++i) {
    const auto& w = toks[i].w;
    bool cue = w == "worse" || w == "aggravated" || w == "aggravate" || w == "triggered" || w == "trigger" || w == "precipitated" ||
               w == "exacerbated" || (w == "after" && i + 1 < toks.size()) || w == "exposure";
    if (cue) aggravationClauses.insert(toks[i].clause);
  }
  for (size_t i = 0; i < toks.size(); ++i) {
    if (used[i]) continue;
    const Entry* hit = nullptr;
    bool fuzzy = false;
    size_t len = 1;
    for (auto& e : lexicon()) {
      size_t n = e.words.size();
      if (i + n > toks.size()) continue;
      bool ok = true;
      for (size_t k = 0; k < n && ok; ++k) ok = toks[i + k].w == e.words[k] && toks[i + k].clause == toks[i].clause;
      if (ok) { hit = &e; len = n; break; }
    }
    if (!hit && toks[i].w.size() >= 4 && !commonWords().count(toks[i].w) &&
        std::none_of(toks[i].w.begin(), toks[i].w.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
      int best = 99;
      for (auto& e : lexicon()) {
        if (e.words.size() != 1 || e.words[0].size() < 5 || e.cat == Cat::FACTOR || e.cat == Cat::SEVERITY) continue;
        // short terms (5 letters) only with the same first letter: "itcy" -> "itchy", but not "grown" -> "brown"
        if (e.words[0].size() == 5 && e.words[0][0] != toks[i].w[0]) continue;
        int d = editDistance(toks[i].w, e.words[0]);
        int allow = e.words[0].size() >= 9 ? 2 : 1;
        if (d <= allow && d < best) { best = d; hit = &e; }
      }
      if (hit) fuzzy = true;
    }
    if (!hit) continue;
    for (size_t k = 0; k < len; ++k) used[i + k] = true;
    std::string ev = evidence(i, i + len - 1);
    if (fuzzy) addCorrection(ev, hit->phrase, catName(hit->cat));
    if (hit->cat == Cat::FACTOR && !aggravationClauses.count(toks[i].clause)) continue;
    if (negated[i]) {
      if (hit->cat == Cat::SYMPTOM || hit->cat == Cat::LESION)
        for (auto& code : hit->codes) negatives.push_back({{"code", code}, {"evidence", ev}});
      if (hit->cat == Cat::PREGNANCY && hit->codes[0] == "PREGNANT") found[Cat::PREGNANCY].push_back({{"code", "NOT_PREGNANT"}, {"evidence", ev}, {"fuzzy", fuzzy}});
      continue;
    }
    for (auto& code : hit->codes) {
      Cat cat = hit->cat;
      if (hit->cat == Cat::LESION && code == "YELLOW") cat = Cat::COLOUR;
      if (seen[cat].count(code)) continue;
      seen[cat].insert(code);
      found[cat].push_back({{"code", code}, {"evidence", ev}, {"fuzzy", fuzzy}});
    }
  }

  // ---- durations of the complaint (not of medicines)
  json duration = nullptr;
  {
    static const std::regex durRe(
        R"(\b(\d+|a|an|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|few|couple of)\s*-?\s*(days?|d|weeks?|wks?|wk|w|months?|mths?|mo|years?|yrs?|yr|y)\b|\b(\d+)\/(7|52|12)\b)",
        std::regex::icase);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), durRe); it != std::sregex_iterator(); ++it) {
      auto& m = *it;
      size_t pos = static_cast<size_t>(m.position(0));
      // skip durations that belong to a medicine on the same line
      size_t lineNo = static_cast<size_t>(std::count(text.begin(), text.begin() + static_cast<long>(pos), '\n'));
      bool medicine = false;
      for (auto& [ln, off] : medSpans)
        if (static_cast<size_t>(ln) == lineNo && off < pos) medicine = true;
      if (medicine) continue;
      int value = 0;
      std::string unit;
      if (m[1].matched) {
        std::string v = lower(m.str(1));
        value = std::isdigit(static_cast<unsigned char>(v[0])) ? std::stoi(v) : numberWord(v == "couple of" ? "couple" : v);
        char u = static_cast<char>(std::tolower(static_cast<unsigned char>(m.str(2)[0])));
        std::string us = lower(m.str(2));
        unit = u == 'd' ? "DAYS" : u == 'w' ? "WEEKS" : (u == 'm') ? "MONTHS" : "YEARS";
        // bare single letters need digits ("5d"), not words ("a d")
        if (us.size() == 1 && !std::isdigit(static_cast<unsigned char>(v[0]))) continue;
      } else {
        value = std::stoi(m.str(3));
        unit = m.str(4) == "7" ? "DAYS" : m.str(4) == "52" ? "WEEKS" : "MONTHS";
      }
      if (value <= 0 || value > 1000) continue;
      duration = {{"value", value}, {"unit", unit}, {"evidence", m.str(0)}};
      break;
    }
  }

  // ---- extent (% body surface area)
  json extent = nullptr;
  {
    static const std::regex bsaRe(R"((\d{1,3}(?:\.\d)?)\s*%\s*(bsa|body surface|of body)?)", std::regex::icase);
    std::smatch m;
    std::string s = text;
    while (std::regex_search(s, m, bsaRe)) {
      if (m[2].matched) {
        double v = std::stod(m.str(1));
        if (v >= 0 && v <= 100) extent = {{"value", v}, {"evidence", m.str(0)}};
        break;
      }
      s = m.suffix();
    }
  }

  auto single = [&](Cat c) -> json { return found[c].empty() ? json(nullptr) : found[c][0]; };
  auto flag = [&](Cat c) -> json { return found[c].empty() ? json(nullptr) : json{{"value", true}, {"evidence", found[c][0]["evidence"]}}; };

  // ---- problem type: explicit hint, else inferred from lesions/colours
  json problem = nullptr;
  if (!found[Cat::PROBLEM].empty()) {
    problem = {{"code", found[Cat::PROBLEM][0]["code"]}, {"evidence", found[Cat::PROBLEM][0]["evidence"]}, {"inferred", false}};
    // "rash" is generic: prefer a more specific hint when present
    for (auto& p : found[Cat::PROBLEM])
      if (p["code"] != "RASH") { problem = {{"code", p["code"]}, {"evidence", p["evidence"]}, {"inferred", false}}; break; }
  }
  auto hasLesion = [&](const std::string& c) { return seen[Cat::LESION].count(c) > 0; };
  if (problem.is_null() || problem["code"] == "RASH") {
    std::string inferred;
    if (hasLesion("WHEAL")) inferred = "HIVES_SWELLING";
    else if (hasLesion("COMEDONE")) inferred = "ACNE_BREAKOUTS";
    else if (hasLesion("ULCER")) inferred = "ULCER_WOUND";
    else if (hasLesion("BULLA")) inferred = "BLISTERS";
    else if (seen[Cat::COLOUR].count("DEPIGMENTED") || ((seen[Cat::COLOUR].count("HYPERPIGMENTED") || seen[Cat::COLOUR].count("HYPOPIGMENTED")) &&
                                                         !hasLesion("PLAQUE") && !hasLesion("PAPULE") && !hasLesion("SCALE")))
      inferred = "PIGMENTATION";
    else if (!seen[Cat::LESION].empty()) inferred = "RASH";
    if (!inferred.empty() && (problem.is_null() || inferred != "RASH"))
      problem = {{"code", inferred}, {"evidence", nullptr}, {"inferred", true}};
  }

  json withLabels = json::object();
  auto label = [&](Cat c, const std::string& code) {
    switch (c) {
      case Cat::SYMPTOM: return labelOf(symptoms(), code);
      case Cat::SITE: return labelOf(bodySites(), code);
      case Cat::LESION: return labelOf(lesionTypes(), code);
      case Cat::COLOUR: return labelOf(colours(), code);
      case Cat::CONFIG: return labelOf(configurations(), code);
      case Cat::DIST: return labelOf(distributions(), code);
      case Cat::FACTOR: return labelOf(factors(), code);
      case Cat::PROBLEM: return labelOf(problemTypes(), code);
      default: return code;
    }
  };
  auto labelled = [&](Cat c) {
    json a = json::array();
    for (auto x : found[c]) {
      x["label"] = label(c, x["code"]);
      a.push_back(x);
    }
    return a;
  };
  for (auto& n : negatives) n["label"] = labelOf(symptoms(), n["code"]) != n["code"].get<std::string>() ? labelOf(symptoms(), n["code"]) : labelOf(lesionTypes(), n["code"]);
  if (!problem.is_null()) problem["label"] = labelOf(problemTypes(), problem["code"]);

  json assessment = {
      {"problemType", problem},
      {"duration", duration},
      {"severity", single(Cat::SEVERITY)},
      {"symptoms", labelled(Cat::SYMPTOM)},
      {"bodySites", labelled(Cat::SITE)},
      {"lesionTypes", labelled(Cat::LESION)},
      {"colours", labelled(Cat::COLOUR)},
      {"configuration", found[Cat::CONFIG].empty() ? json(nullptr) : labelled(Cat::CONFIG)[0]},
      {"distribution", found[Cat::DIST].empty() ? json(nullptr) : labelled(Cat::DIST)[0]},
      {"spreading", flag(Cat::SPREADING)},
      {"recurring", flag(Cat::RECURRING)},
      {"aggravatingFactors", labelled(Cat::FACTOR)},
      {"extentBsaPercent", extent},
      {"pregnancyStatus", single(Cat::PREGNANCY)},
  };

  json out = {{"sections", sections}, {"assessment", assessment}, {"negatives", negatives}, {"diagnoses", dx},
              {"excludedDiagnoses", dxExcluded}, {"medicines", meds}, {"corrections", corrections}, {"engineVersion", "note-extract-1.0"}};
  json warnings = json::array();
  if (text.find_first_not_of(" \t\r\n") == std::string::npos) warnings.push_back("The note is empty.");
  if (!corrections.empty()) warnings.push_back("Some words were matched approximately. Check the suggested corrections.");
  if (dx.size() > 1) warnings.push_back("More than one diagnosis is written. Choose which one is the working diagnosis.");
  out["warnings"] = warnings;
  return out;
}

}  // namespace cf::derm
