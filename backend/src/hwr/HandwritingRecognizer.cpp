#include "HandwritingRecognizer.hpp"

#include <cstdlib>
#include <nlohmann/json.hpp>

#include "security/Crypto.hpp"

namespace cf::hwr {

using json = nlohmann::json;

static std::string env(const char* k, const std::string& def = "") {
  const char* v = std::getenv(k);
  return v && *v ? std::string(v) : def;
}

RecognitionResult GoogleVisionRecognizer::parseResponse(int status, const std::string& body) {
  json j;
  try {
    j = json::parse(body);
  } catch (...) {
    return {false, "", "Recognition service returned an unreadable response (HTTP " + std::to_string(status) + ")"};
  }
  if (status != 200) {
    std::string msg = j.contains("error") ? j["error"].value("message", "") : "";
    return {false, "", "Recognition service error (HTTP " + std::to_string(status) + ")" + (msg.empty() ? "" : ": " + msg)};
  }
  if (!j.contains("responses") || !j["responses"].is_array() || j["responses"].empty())
    return {false, "", "Recognition service returned no result"};
  auto& r = j["responses"][0];
  if (r.contains("error")) return {false, "", "Recognition failed: " + r["error"].value("message", "unknown error")};
  if (r.contains("fullTextAnnotation")) return {true, r["fullTextAnnotation"].value("text", ""), ""};
  return {true, "", ""};  // no text found on the page
}

RecognitionResult GoogleVisionRecognizer::recognize(const std::string& png) {
  json req = {{"requests", json::array({{{"image", {{"content", crypto::base64Encode(png)}}},
                                         {"features", json::array({{{"type", "DOCUMENT_TEXT_DETECTION"}}})},
                                         // documented hint for handwritten English
                                         {"imageContext", {{"languageHints", json::array({"en-t-i0-handwrit"})}}}}})}};
  try {
    auto res = httpPost(m_endpoint + "/v1/images:annotate", {{"Content-Type", "application/json"}, {"X-Goog-Api-Key", m_key}},
                        req.dump(), m_opts);
    return parseResponse(res.status, res.body);
  } catch (const std::exception& e) {
    return {false, "", std::string("Could not reach the recognition service: ") + e.what()};
  }
}

RecognitionResult HttpRecognizer::parseResponse(int status, const std::string& body) {
  json j;
  try {
    j = json::parse(body);
  } catch (...) {
    return {false, "", "Recognition service returned an unreadable response (HTTP " + std::to_string(status) + ")"};
  }
  if (status != 200) return {false, "", "Recognition service error (HTTP " + std::to_string(status) + "): " + j.value("error", std::string(""))};
  if (!j.contains("text") || !j["text"].is_string()) return {false, "", "Recognition response has no 'text' field"};
  return {true, j["text"].get<std::string>(), ""};
}

RecognitionResult HttpRecognizer::recognize(const std::string& png) {
  json req = {{"imageBase64", crypto::base64Encode(png)}, {"mimeType", "image/png"}, {"languageHints", json::array({"en"})},
              {"purpose", "clinical-handwriting"}};
  std::map<std::string, std::string> headers = {{"Content-Type", "application/json"}};
  if (!m_token.empty()) headers["Authorization"] = "Bearer " + m_token;
  try {
    auto res = httpPost(m_url, headers, req.dump(), m_opts);
    return parseResponse(res.status, res.body);
  } catch (const std::exception& e) {
    return {false, "", std::string("Could not reach the recognition service: ") + e.what()};
  }
}

std::shared_ptr<HandwritingRecognizer> makeRecognizerFromEnv() {
  std::string provider = env("HWR_PROVIDER", "none");
  HttpOptions opts;
  opts.caFile = env("HWR_CA_FILE");
  opts.timeoutSeconds = std::atoi(env("HWR_TIMEOUT_SECONDS", "30").c_str());
  if (provider == "google-vision" && !env("HWR_GOOGLE_API_KEY").empty())
    return std::make_shared<GoogleVisionRecognizer>(env("HWR_GOOGLE_API_KEY"), env("HWR_GOOGLE_ENDPOINT", "https://vision.googleapis.com"), opts);
  if (provider == "http" && !env("HWR_HTTP_URL").empty())
    return std::make_shared<HttpRecognizer>(env("HWR_HTTP_URL"), env("HWR_HTTP_TOKEN"), env("HWR_HTTP_LABEL"), opts);
  return std::make_shared<DisabledRecognizer>();
}

}  // namespace cf::hwr
