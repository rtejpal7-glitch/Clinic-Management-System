#pragma once
// Prescribing-pattern aggregation (pure logic) for
//   Panel B — the doctor's own previous comparable cases, and
//   Panel C — de-identified cases from other doctors in participating clinics.
//
// Frequencies describe what was prescribed. They say nothing about whether a
// treatment works. Disclosure controls for Panel C (minimum cases, doctors,
// clinics; no dominant clinic; small cells grouped) are applied here so they can
// be unit-tested independently of the database.

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cf::derm {

struct PatternCase {
  std::vector<std::string> generics;            // distinct generic codes prescribed
  nlohmann::json items = nlohmann::json::array();  // [{generic,name,route,frequency,durationDays}]
  std::string outcome;                          // RESOLVED | IMPROVED | UNCHANGED | WORSENED | ""
  std::string doctorKey, facilityKey;
  std::string period;                           // YYYY-MM-DD (own) or YYYY-MM-01 (network)
};

struct NetworkThresholds {
  int minCases = 10;
  int minDoctors = 3;
  int minFacilities = 2;
  double maxFacilityShare = 0.70;
  int minCellCount = 3;          // medicines used in fewer cases are grouped as "Other"
  int minOutcomesToShow = 5;
};

struct OwnThresholds {
  int minCases = 3;
  int minOutcomesToShow = 3;
};

nlohmann::json summarizeOwn(const std::vector<PatternCase>& cases, const OwnThresholds& t = {});
nlohmann::json summarizeNetwork(const std::vector<PatternCase>& cases, const NetworkThresholds& t = {});

}  // namespace cf::derm
