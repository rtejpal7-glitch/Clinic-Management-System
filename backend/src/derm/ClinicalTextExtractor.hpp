#pragma once
// Turns a doctor's (transcribed) consultation note into proposed structured
// fields. Pure and deterministic: a medical lexicon of lay and clinical terms,
// Indian clinical shorthand (c/o, o/e, h/o, 2/52, BD, TDS ...), negation handling
// ("no fever", "denies itch"), section headings, and tolerant matching for words
// that handwriting recognition commonly gets slightly wrong.
//
// Everything it returns is a PROPOSAL with the evidence text it came from. The
// doctor reviews, corrects and explicitly accepts each item; nothing here writes
// to the clinical record.

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cf::derm {

struct KnownMedicine {
  std::string genericCode;
  std::string genericName;  // e.g. "Fexofenadine", "Benzoyl Peroxide"
};

struct KnownDiagnosis {
  std::string code;      // ICD-10
  std::string display;
  std::vector<std::string> synonyms;  // lower-case phrases, e.g. "hives", "ringworm"
};

// Built-in dermatology diagnosis synonyms (ICD-10) used when the doctor writes a diagnosis.
const std::vector<KnownDiagnosis>& dermDiagnosisLexicon();

nlohmann::json extractClinicalText(const std::string& text, const std::vector<KnownMedicine>& medicines,
                                   const std::vector<KnownDiagnosis>& diagnoses = dermDiagnosisLexicon());

// Levenshtein distance (exposed for tests)
int editDistance(const std::string& a, const std::string& b);

}  // namespace cf::derm
