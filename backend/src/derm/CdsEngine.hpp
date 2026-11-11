#pragma once
// Dermatology clinical decision support — pure, deterministic and explainable.
//
// What it does: compares the coded findings of a consultation with APPROVED
// knowledge-base entries and lists conditions whose documented features match,
// with the matched and missing findings, distinguishing questions, red flags,
// and guideline treatment options with their references.
//
// What it does not do: diagnose, rank by probability, or choose a medicine. The
// score is the share of a condition's weighted features that were recorded; it
// is a reading aid for the clinician, not a likelihood.

#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cf::derm {

inline const char* CDS_ENGINE_VERSION = "derm-rules-1.0";

struct DermFacts {
  std::string problemType;
  std::optional<int> durationDays;
  std::string severity;  // MILD | MODERATE | SEVERE | ""
  std::set<std::string> symptoms, sites, lesions, colours, aggravating;
  std::string configuration, distribution;
  std::optional<bool> spreading, recurring;
  std::optional<double> extentPercent;
  std::optional<int> ageYears;
  bool historyAtopy = false;
};

struct CdsFeature {
  std::string token;
  int weight = 1;
};
struct CdsContentItem {
  std::string kind;  // OBSERVATION | QUESTION | TREATMENT | REFERRAL
  std::string text;
  std::vector<std::string> generics;
  std::string line;
  std::vector<std::string> severities;
  std::string reference;
};
struct CdsCondition {
  std::string code, name, icd10, summary;
  nlohmann::json refs = nlohmann::json::array();
  int contentVersion = 1;
  std::vector<CdsFeature> features;
  std::vector<CdsContentItem> content;
};
struct CdsRedFlag {
  std::string code, title, guidance, reference;
  nlohmann::json criteria;
};

// All tokens the facts produce (used for matching features and red-flag criteria)
std::set<std::string> factTokens(const DermFacts& f);
// Required minimum information; empty when sufficient.
std::vector<std::string> missingEssentials(const DermFacts& f);
bool redFlagMatches(const CdsRedFlag& rf, const DermFacts& f, const std::set<std::string>& tokens);

nlohmann::json evaluate(const DermFacts& facts, const std::vector<CdsCondition>& approvedConditions,
                        const std::vector<CdsRedFlag>& approvedRedFlags, int itemsAwaitingReview);

}  // namespace cf::derm
