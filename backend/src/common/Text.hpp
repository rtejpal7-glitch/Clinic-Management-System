#pragma once
// Small string helpers shared by services.

#include <optional>
#include <regex>
#include <string>

#include "oatpp/core/Types.hpp"

namespace cf::text {

inline std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

inline std::string trimmed(const oatpp::String& s) { return s ? trim(*s) : ""; }

// Trimmed value or nullopt when null/blank — used to store NULL instead of "".
inline std::optional<std::string> opt(const oatpp::String& s) {
  if (!s) return std::nullopt;
  auto t = trim(*s);
  if (t.empty()) return std::nullopt;
  return t;
}

inline std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
inline std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline bool isEmail(const std::string& s) {
  static const std::regex re(R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)");
  return s.size() <= 254 && std::regex_match(s, re);
}
inline bool isIndianMobile(const std::string& s) {
  static const std::regex re("^[6-9][0-9]{9}$");
  return std::regex_match(s, re);
}
inline bool isIsoDate(const std::string& s) {
  static const std::regex re(R"(^\d{4}-\d{2}-\d{2}$)");
  return std::regex_match(s, re);
}
inline bool isUuid(const std::string& s) {
  static const std::regex re("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$");
  return std::regex_match(s, re);
}
inline bool oneOf(const std::string& v, std::initializer_list<const char*> options) {
  for (auto o : options)
    if (v == o) return true;
  return false;
}

inline std::string join(const oatpp::Vector<oatpp::String>& v, const std::string& sep = ",") {
  std::string out;
  if (!v) return out;
  for (auto& s : *v) {
    if (!out.empty()) out += sep;
    out += s ? std::string(*s) : "";
  }
  return out;
}

// Postgres text[] literal from a vector of simple tokens (validated upstream).
inline std::string pgArray(const oatpp::Vector<oatpp::String>& v) {
  std::string out = "{";
  bool first = true;
  if (v)
    for (auto& s : *v) {
      if (!first) out += ",";
      first = false;
      out += "\"";
      for (char c : std::string(*s)) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
      }
      out += "\"";
    }
  return out + "}";
}

}  // namespace cf::text
