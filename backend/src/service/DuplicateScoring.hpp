#pragma once
// Pure duplicate-patient scoring logic (unit-testable, no database).
// Candidate rows are pre-filtered in SQL (same mobile, same DOB, or similar
// name via pg_trgm); this function applies the clinic's configurable weights.

#include <string>
#include <vector>

namespace cf {

struct DuplicateRules {
  int threshold = 60;
  double nameSimilarityMin = 0.45;
  int weightMobile = 40;
  int weightDateOfBirth = 30;
  int weightName = 30;
  int weightGender = 0;
};

struct DuplicateProbe {
  std::string fullName;  // normalized lower-case
  std::string dateOfBirth;
  std::string mobile;
  std::string gender;
};

struct DuplicateCandidateFacts {
  std::string dateOfBirth;
  std::string mobile;
  std::string gender;
  double nameSimilarity = 0.0;  // pg_trgm similarity 0..1
};

struct DuplicateScore {
  int score = 0;
  std::vector<std::string> matchedOn;
  bool isPossibleDuplicate = false;
};

DuplicateScore scoreDuplicate(const DuplicateRules& rules, const DuplicateProbe& probe,
                              const DuplicateCandidateFacts& candidate);

}  // namespace cf
