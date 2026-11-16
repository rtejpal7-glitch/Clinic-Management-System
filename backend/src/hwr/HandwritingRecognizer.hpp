#pragma once
// Port for handwriting recognition (handwritten note image -> text).
//
// Providers (selected with HWR_PROVIDER):
//   none          - default. No automatic recognition; the doctor types or uses
//                   iPadOS Scribble beside the handwriting. Nothing leaves the server.
//   google-vision - Google Cloud Vision DOCUMENT_TEXT_DETECTION with a handwriting
//                   language hint. Sends the page image to Google: the clinic needs a
//                   data-processing agreement and must decide on data residency.
//                   HWR_GOOGLE_API_KEY, optional HWR_GOOGLE_ENDPOINT (e.g. eu-vision.googleapis.com).
//   http          - a recognition service you run yourself (data stays in your
//                   infrastructure). Contract documented in docs/HANDWRITING.md.
//                   HWR_HTTP_URL, optional HWR_HTTP_TOKEN, HWR_HTTP_LABEL, HWR_CA_FILE.

#include <memory>
#include <string>

#include "hwr/HttpsClient.hpp"

namespace cf::hwr {

struct RecognitionResult {
  bool ok = false;
  std::string text;
  std::string error;  // user-presentable reason when !ok
};

class HandwritingRecognizer {
 public:
  virtual ~HandwritingRecognizer() = default;
  virtual std::string id() const = 0;            // NONE | GOOGLE_VISION | HTTP
  virtual std::string description() const = 0;  // shown to the doctor
  virtual bool sendsDataExternally() const = 0;
  virtual bool enabled() const { return true; }
  virtual RecognitionResult recognize(const std::string& pngBytes) = 0;
};

class DisabledRecognizer : public HandwritingRecognizer {
 public:
  std::string id() const override { return "NONE"; }
  std::string description() const override { return "No automatic recognition configured"; }
  bool sendsDataExternally() const override { return false; }
  bool enabled() const override { return false; }
  RecognitionResult recognize(const std::string&) override { return {false, "", "Automatic handwriting recognition is not configured"}; }
};

class GoogleVisionRecognizer : public HandwritingRecognizer {
 public:
  GoogleVisionRecognizer(std::string apiKey, std::string endpoint, HttpOptions opts)
      : m_key(std::move(apiKey)), m_endpoint(std::move(endpoint)), m_opts(std::move(opts)) {}
  std::string id() const override { return "GOOGLE_VISION"; }
  std::string description() const override { return "Google Cloud Vision (" + m_endpoint + ")"; }
  bool sendsDataExternally() const override { return true; }
  RecognitionResult recognize(const std::string& pngBytes) override;
  static RecognitionResult parseResponse(int status, const std::string& body);  // exposed for tests

 private:
  std::string m_key, m_endpoint;
  HttpOptions m_opts;
};

class HttpRecognizer : public HandwritingRecognizer {
 public:
  HttpRecognizer(std::string url, std::string token, std::string label, HttpOptions opts)
      : m_url(std::move(url)), m_token(std::move(token)), m_label(std::move(label)), m_opts(std::move(opts)) {}
  std::string id() const override { return "HTTP"; }
  std::string description() const override { return m_label.empty() ? "Self-hosted recognition service" : m_label; }
  bool sendsDataExternally() const override { return true; }
  RecognitionResult recognize(const std::string& pngBytes) override;
  static RecognitionResult parseResponse(int status, const std::string& body);

 private:
  std::string m_url, m_token, m_label;
  HttpOptions m_opts;
};

// Reads HWR_* environment variables. Never throws for a missing optional provider.
std::shared_ptr<HandwritingRecognizer> makeRecognizerFromEnv();

}  // namespace cf::hwr
