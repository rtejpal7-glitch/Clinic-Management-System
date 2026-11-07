#pragma once
// Allowed appointment status transitions (pure logic, unit-tested).
//
//   SCHEDULED ──check-in──▶ CHECKED_IN ──start──▶ IN_CONSULTATION ──finalize──▶ COMPLETED
//       │                       │
//       ├──cancel──▶ CANCELLED ◀┘
//       └──no-show─▶ NO_SHOW

#include <set>
#include <string>
#include <utility>

namespace cf {

inline bool canTransition(const std::string& from, const std::string& to) {
  static const std::set<std::pair<std::string, std::string>> allowed = {
      {"SCHEDULED", "CHECKED_IN"},       {"SCHEDULED", "CANCELLED"},  {"SCHEDULED", "NO_SHOW"},
      {"CHECKED_IN", "IN_CONSULTATION"}, {"CHECKED_IN", "CANCELLED"}, {"IN_CONSULTATION", "COMPLETED"},
  };
  return allowed.count({from, to}) > 0;
}

inline bool isTerminal(const std::string& status) {
  return status == "COMPLETED" || status == "CANCELLED" || status == "NO_SHOW";
}

}  // namespace cf
