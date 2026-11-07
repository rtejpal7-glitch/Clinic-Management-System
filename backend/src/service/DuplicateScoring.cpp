#include "DuplicateScoring.hpp"

#include <algorithm>
#include <cmath>

namespace cf {

DuplicateScore scoreDuplicate(const DuplicateRules& rules, const DuplicateProbe& probe,
                              const DuplicateCandidateFacts& c) {
  DuplicateScore s;
  if (!probe.mobile.empty() && probe.mobile == c.mobile) {
    s.score += rules.weightMobile;
    s.matchedOn.push_back("mobile");
  }
  if (!probe.dateOfBirth.empty() && probe.dateOfBirth == c.dateOfBirth) {
    s.score += rules.weightDateOfBirth;
    s.matchedOn.push_back("dateOfBirth");
  }
  if (c.nameSimilarity >= rules.nameSimilarityMin) {
    // Scale the name weight by similarity so near-identical names count fully
    // and borderline matches count partially.
    double factor = c.nameSimilarity >= 0.85 ? 1.0 : c.nameSimilarity;
    s.score += static_cast<int>(std::lround(rules.weightName * factor));
    s.matchedOn.push_back("name");
  }
  if (rules.weightGender > 0 && !probe.gender.empty() && probe.gender == c.gender) {
    s.score += rules.weightGender;
    s.matchedOn.push_back("gender");
  }
  s.score = std::min(s.score, 100);
  s.isPossibleDuplicate = s.score >= rules.threshold;
  return s;
}

}  // namespace cf
