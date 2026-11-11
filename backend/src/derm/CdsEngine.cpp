#include "CdsEngine.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "DermVocabulary.hpp"

namespace cf::derm {

using json = nlohmann::json;

std::set<std::string> factTokens(const DermFacts& f) {
  std::set<std::string> t;
  if (!f.problemType.empty()) t.insert("PROBLEM:" + f.problemType);
  for (auto& s : f.symptoms) t.insert("SYMPTOM:" + s);
  for (auto& s : f.sites) t.insert("SITE:" + s);
  for (auto& s : f.lesions) t.insert("LESION:" + s);
  for (auto& s : f.colours) t.insert("COLOUR:" + s);
  for (auto& s : f.aggravating) t.insert("AGGRAVATING:" + s);
  if (!f.configuration.empty()) t.insert("CONFIG:" + f.configuration);
  if (!f.distribution.empty()) t.insert("DIST:" + f.distribution);
  if (f.spreading && *f.spreading) t.insert("SPREADING");
  if (f.recurring && *f.recurring) t.insert("RECURRING");
  if (f.ageYears) t.insert("AGE:" + cdsAgeGroup(*f.ageYears));
  if (f.historyAtopy) t.insert("HISTORY:ATOPY");
  return t;
}

std::vector<std::string> missingEssentials(const DermFacts& f) {
  std::vector<std::string> m;
  if (f.problemType.empty()) m.push_back("Type of skin problem");
  if (f.lesions.empty() && f.sites.empty()) m.push_back("Lesion type or body location");
  if (!f.durationDays) m.push_back("Duration");
  return m;
}

// Tokens with parameters are evaluated against the facts; plain tokens by membership.
static bool tokenHolds(const std::string& token, const DermFacts& f, const std::set<std::string>& tokens) {
  if (token.rfind("NO_SYMPTOM:", 0) == 0) {
    // Absence only counts once the problem has been characterised
    return !f.problemType.empty() && !f.symptoms.count(token.substr(11));
  }
  if (token.rfind("EXTENT_GE:", 0) == 0)
    return f.extentPercent && *f.extentPercent >= std::stod(token.substr(10));
  if (token.rfind("DURATION_GE_DAYS:", 0) == 0)
    return f.durationDays && *f.durationDays >= std::stoi(token.substr(17));
  return tokens.count(token) > 0;
}

bool redFlagMatches(const CdsRedFlag& rf, const DermFacts& f, const std::set<std::string>& tokens) {
  const auto& c = rf.criteria;
  bool hasAll = c.contains("all") && c["all"].is_array() && !c["all"].empty();
  bool hasAny = c.contains("any") && c["any"].is_array() && !c["any"].empty();
  if (!hasAll && !hasAny) return false;
  if (hasAll)
    for (auto& t : c["all"])
      if (!tokenHolds(t.get<std::string>(), f, tokens)) return false;
  if (hasAny) {
    bool any = false;
    for (auto& t : c["any"])
      if (tokenHolds(t.get<std::string>(), f, tokens)) { any = true; break; }
    if (!any) return false;
  }
  return true;
}

static std::string strengthLabel(double s) {
  if (s >= 0.55) return "Strong match";
  if (s >= 0.35) return "Moderate match";
  return "Weak match";
}

json evaluate(const DermFacts& facts, const std::vector<CdsCondition>& conditions, const std::vector<CdsRedFlag>& redFlags,
              int itemsAwaitingReview) {
  auto tokens = factTokens(facts);
  json out = {{"engineVersion", CDS_ENGINE_VERSION},
              {"redFlags", json::array()},
              {"conditions", json::array()},
              {"questions", json::array()},
              {"treatments", json::array()},
              {"referrals", json::array()},
              {"missing", json::array()},
              {"itemsAwaitingReview", itemsAwaitingReview},
              {"recordedFindings", json::array()}};
  for (auto& t : tokens) out["recordedFindings"].push_back(tokenLabel(t));

  // Red flags are always evaluated, even when information is incomplete.
  for (auto& rf : redFlags) {
    if (redFlagMatches(rf, facts, tokens))
      out["redFlags"].push_back({{"code", rf.code}, {"title", rf.title}, {"guidance", rf.guidance}, {"reference", rf.reference}});
  }

  if (conditions.empty()) {
    out["status"] = "NO_APPROVED_CONTENT";
    out["message"] = itemsAwaitingReview > 0
                         ? "No clinically approved decision-support content is available yet. " +
                               std::to_string(itemsAwaitingReview) + " draft item(s) are awaiting clinical review."
                         : "No approved decision-support content is available for this specialty.";
    return out;
  }
  auto missing = missingEssentials(facts);
  if (!missing.empty()) {
    out["status"] = "INSUFFICIENT_DATA";
    for (auto& m : missing) out["missing"].push_back(m);
    out["message"] = "Not enough information to compare with the knowledge base. Record the missing items to see possible conditions.";
    return out;
  }

  struct Scored {
    const CdsCondition* c;
    double score;
    json matched, notMatched;
  };
  std::vector<Scored> scored;
  for (auto& c : conditions) {
    int total = 0, got = 0;
    json matched = json::array(), notMatched = json::array();
    for (auto& ft : c.features) {
      total += ft.weight;
      if (tokenHolds(ft.token, facts, tokens)) {
        got += ft.weight;
        matched.push_back({{"finding", tokenLabel(ft.token)}, {"weight", ft.weight}});
      } else if (ft.weight >= 2) {
        notMatched.push_back({{"finding", tokenLabel(ft.token)}, {"weight", ft.weight}});
      }
    }
    if (total == 0 || got == 0) continue;
    double s = static_cast<double>(got) / total;
    if (s < 0.25) continue;
    scored.push_back({&c, s, matched, notMatched});
  }
  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.c->code < b.c->code;
  });
  if (scored.size() > 5) scored.resize(5);

  if (scored.empty()) {
    out["status"] = "NO_MATCH";
    out["message"] = "The recorded findings do not match any approved condition profile closely. "
                     "This does not exclude any diagnosis.";
    return out;
  }
  out["status"] = "OK";
  out["message"] = "Possible conditions to consider, based on recorded findings. Not a diagnosis.";

  std::set<std::string> seenQuestions;
  int rank = 0;
  for (auto& s : scored) {
    const auto& c = *s.c;
    json cj = {{"code", c.code}, {"name", c.name}, {"icd10", c.icd10}, {"summary", c.summary},
               {"score", std::round(s.score * 100) / 100.0}, {"strength", strengthLabel(s.score)},
               {"matched", s.matched}, {"notRecorded", s.notMatched}, {"observations", json::array()},
               {"references", c.refs}, {"contentVersion", c.contentVersion}};
    for (auto& it : c.content) {
      if (it.kind == "OBSERVATION") cj["observations"].push_back(it.text);
    }
    out["conditions"].push_back(cj);
    // Questions, treatments and referrals from the three closest matches
    if (rank < 3) {
      for (auto& it : c.content) {
        if (it.kind == "QUESTION" && !seenQuestions.count(it.text) && out["questions"].size() < 10) {
          seenQuestions.insert(it.text);
          out["questions"].push_back({{"text", it.text}, {"condition", c.name}});
        } else if (it.kind == "TREATMENT") {
          bool sevOk = it.severities.empty() || facts.severity.empty() ||
                       std::find(it.severities.begin(), it.severities.end(), facts.severity) != it.severities.end();
          if (!sevOk) continue;
          out["treatments"].push_back({{"conditionCode", c.code}, {"condition", c.name}, {"text", it.text},
                                       {"line", it.line}, {"generics", it.generics}, {"reference", it.reference}});
        } else if (it.kind == "REFERRAL") {
          out["referrals"].push_back({{"condition", c.name}, {"text", it.text}, {"reference", it.reference}});
        }
      }
    }
    ++rank;
  }
  return out;
}

}  // namespace cf::derm
