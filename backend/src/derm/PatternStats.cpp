#include "PatternStats.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace cf::derm {

using json = nlohmann::json;

namespace {

double pct(int n, int d) { return d == 0 ? 0.0 : std::round(1000.0 * n / d) / 10.0; }

struct MedAgg {
  std::string generic, name;
  int count = 0;
  std::map<std::string, int> regimens;  // "route|frequency|days" -> count
  std::map<std::string, int> outcomes;
  int outcomesRecorded = 0;
  std::set<std::string> doctors, facilities;
};

std::map<std::string, MedAgg> aggregate(const std::vector<PatternCase>& cases) {
  std::map<std::string, MedAgg> meds;
  for (auto& c : cases) {
    std::set<std::string> seen;
    for (auto& it : c.items) {
      std::string g = it.value("generic", "");
      if (g.empty() || seen.count(g)) continue;
      seen.insert(g);
      auto& m = meds[g];
      m.generic = g;
      if (m.name.empty()) m.name = it.value("name", g);
      m.count++;
      std::string days = it.contains("durationDays") && it["durationDays"].is_number() ? std::to_string(it["durationDays"].get<int>()) : "";
      m.regimens[it.value("route", "") + "|" + it.value("frequency", "") + "|" + days]++;
      if (!c.outcome.empty()) {
        m.outcomes[c.outcome]++;
        m.outcomesRecorded++;
      }
      m.doctors.insert(c.doctorKey);
      m.facilities.insert(c.facilityKey);
    }
  }
  return meds;
}

json regimenOf(const MedAgg& m) {
  std::string best;
  int n = 0;
  for (auto& [k, v] : m.regimens)
    if (v > n) { best = k; n = v; }
  auto p1 = best.find('|'), p2 = best.find('|', p1 + 1);
  json r = {{"route", best.substr(0, p1)}, {"frequency", best.substr(p1 + 1, p2 - p1 - 1)}, {"cases", n}};
  std::string d = best.substr(p2 + 1);
  if (!d.empty()) r["durationDays"] = std::stoi(d);
  return r;
}

json outcomesOf(const MedAgg& m, int minShown) {
  if (m.outcomesRecorded < minShown)
    return {{"recorded", m.outcomesRecorded}, {"shown", false},
            {"note", "Too few recorded follow-up outcomes to summarise"}};
  json o = {{"recorded", m.outcomesRecorded}, {"shown", true}};
  for (auto k : {"RESOLVED", "IMPROVED", "UNCHANGED", "WORSENED"}) {
    int n = m.outcomes.count(k) ? m.outcomes.at(k) : 0;
    o[k] = {{"count", n}, {"percent", pct(n, m.outcomesRecorded)}};
  }
  return o;
}

json period(const std::vector<PatternCase>& cases) {
  std::string lo, hi;
  for (auto& c : cases) {
    if (c.period.empty()) continue;
    if (lo.empty() || c.period < lo) lo = c.period;
    if (hi.empty() || c.period > hi) hi = c.period;
  }
  return {{"from", lo}, {"to", hi}};
}

std::vector<MedAgg> sorted(const std::map<std::string, MedAgg>& m) {
  std::vector<MedAgg> v;
  for (auto& [_, a] : m) v.push_back(a);
  std::sort(v.begin(), v.end(), [](const MedAgg& a, const MedAgg& b) {
    return a.count != b.count ? a.count > b.count : a.generic < b.generic;
  });
  return v;
}

}  // namespace

json summarizeOwn(const std::vector<PatternCase>& cases, const OwnThresholds& t) {
  int n = static_cast<int>(cases.size());
  json out = {{"caseCount", n}, {"minimumCases", t.minCases}, {"period", period(cases)}, {"medications", json::array()}};
  if (n < t.minCases) {
    out["status"] = "INSUFFICIENT_DATA";
    out["message"] = "Fewer than " + std::to_string(t.minCases) + " of your previous finalized cases are comparable (" +
                     std::to_string(n) + " found).";
    return out;
  }
  out["status"] = "OK";
  int withOutcome = 0;
  for (auto& c : cases) withOutcome += c.outcome.empty() ? 0 : 1;
  out["casesWithOutcome"] = withOutcome;
  for (auto& m : sorted(aggregate(cases))) {
    out["medications"].push_back({{"generic", m.generic}, {"name", m.name}, {"cases", m.count}, {"percent", pct(m.count, n)},
                                  {"usualRegimen", regimenOf(m)}, {"outcomes", outcomesOf(m, t.minOutcomesToShow)}});
  }
  return out;
}

json summarizeNetwork(const std::vector<PatternCase>& cases, const NetworkThresholds& t) {
  int n = static_cast<int>(cases.size());
  std::set<std::string> doctors, facilities;
  std::map<std::string, int> perFacility;
  for (auto& c : cases) {
    doctors.insert(c.doctorKey);
    facilities.insert(c.facilityKey);
    perFacility[c.facilityKey]++;
  }
  int maxFac = 0;
  for (auto& [_, v] : perFacility) maxFac = std::max(maxFac, v);
  json out = {{"thresholds", {{"minCases", t.minCases}, {"minDoctors", t.minDoctors}, {"minClinics", t.minFacilities},
                              {"maxShareFromOneClinicPercent", static_cast<int>(t.maxFacilityShare * 100)},
                              {"minCasesPerMedicine", t.minCellCount}}},
              {"medications", json::array()}};
  std::vector<std::string> reasons;
  if (n < t.minCases) reasons.push_back("fewer than " + std::to_string(t.minCases) + " comparable cases");
  if (static_cast<int>(doctors.size()) < t.minDoctors) reasons.push_back("fewer than " + std::to_string(t.minDoctors) + " contributing doctors");
  if (static_cast<int>(facilities.size()) < t.minFacilities) reasons.push_back("fewer than " + std::to_string(t.minFacilities) + " contributing clinics");
  if (n > 0 && static_cast<double>(maxFac) / n > t.maxFacilityShare) reasons.push_back("one clinic contributes most of the cases");
  if (!reasons.empty()) {
    // Do not reveal exact small counts when suppressed.
    out["status"] = "INSUFFICIENT_DATA";
    std::string r;
    for (auto& x : reasons) r += (r.empty() ? "" : "; ") + x;
    out["message"] = "Network statistics are withheld: " + r + ".";
    out["caseCount"] = nullptr;
    return out;
  }
  out["status"] = "OK";
  out["caseCount"] = n;
  out["doctorCount"] = static_cast<int>(doctors.size());
  out["clinicCount"] = static_cast<int>(facilities.size());
  out["period"] = period(cases);
  int otherCases = 0;
  for (auto& m : sorted(aggregate(cases))) {
    if (m.count < t.minCellCount || static_cast<int>(m.doctors.size()) < 2) {
      otherCases += m.count;
      continue;
    }
    out["medications"].push_back({{"generic", m.generic}, {"name", m.name}, {"cases", m.count}, {"percent", pct(m.count, n)},
                                  {"doctors", static_cast<int>(m.doctors.size())}, {"usualRegimen", regimenOf(m)},
                                  {"outcomes", outcomesOf(m, t.minOutcomesToShow)}});
  }
  if (otherCases > 0) out["otherMedicinesGrouped"] = {{"note", "Medicines used in very few cases or by a single doctor are not listed individually"}};
  return out;
}

}  // namespace cf::derm
