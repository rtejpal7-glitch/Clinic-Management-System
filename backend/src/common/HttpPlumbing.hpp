#pragma once
// Router-level error handler (404s, protocol errors) and a response interceptor
// that adds security headers to every response.

#include "controller/BaseController.hpp"
#include "oatpp/web/server/handler/ErrorHandler.hpp"
#include "oatpp/web/server/interceptor/RequestInterceptor.hpp"
#include "oatpp/web/server/interceptor/ResponseInterceptor.hpp"

namespace cf {

class JsonErrorHandler : public oatpp::web::server::handler::ErrorHandler {
 public:
  explicit JsonErrorHandler(std::shared_ptr<oatpp::data::mapping::ObjectMapper> om) : m_om(std::move(om)) {}
  std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> handleError(
      const oatpp::web::protocol::http::Status& status, const oatpp::String& message, const Headers& headers) override {
    std::string code = status.code == 404 ? "NOT_FOUND" : (status.code >= 500 ? "INTERNAL_ERROR" : "BAD_REQUEST");
    std::string msg = status.code == 404 ? "Resource not found"
                      : status.code >= 500 ? "An unexpected error occurred"
                                           : (message ? std::string(*message) : "Bad request");
    if (status.code >= 500 && message) OATPP_LOGE("HTTP", "%d %s", status.code, message->c_str());
    auto resp = BaseController::errorJson(m_om, status.code, code, msg);
    for (const auto& pair : headers.getAll()) resp->putHeader(pair.first.toString(), pair.second.toString());
    return resp;
  }

 private:
  std::shared_ptr<oatpp::data::mapping::ObjectMapper> m_om;
};

// Reads every request body exactly once, before routing, and stores it as bundle
// data. Endpoints never leave unread bytes on a keep-alive connection (which would
// corrupt the next request), and oversized bodies are rejected up front.
class BodyCaptureInterceptor : public oatpp::web::server::interceptor::RequestInterceptor {
 public:
  explicit BodyCaptureInterceptor(std::shared_ptr<oatpp::data::mapping::ObjectMapper> om) : m_om(std::move(om)) {}
  static constexpr v_int64 MAX_BODY = 1024 * 1024;
  // Clinical photograph uploads (base64 JSON) get a larger, still bounded, limit.
  static constexpr v_int64 MAX_PHOTO_BODY = 9 * 1024 * 1024;
  std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request) override {
    std::string path = request->getStartingLine().path.toString();
    std::string method = request->getStartingLine().method.toString();
    bool photo = method == "POST" && path.size() > 7 && path.compare(path.size() - 7, 7, "/photos") == 0;
    const v_int64 limit = photo ? MAX_PHOTO_BODY : MAX_BODY;
    const char* limitText = photo ? "Request body exceeds 9 MB" : "Request body exceeds 1 MB";
    auto len = request->getHeader("Content-Length");
    if (len) {
      v_int64 n = 0;
      try { n = std::stoll(*len); } catch (...) { n = -1; }
      if (n < 0) return BaseController::errorJson(m_om, 400, "BAD_REQUEST", "Invalid Content-Length");
      if (n > limit) return BaseController::errorJson(m_om, 413, "PAYLOAD_TOO_LARGE", limitText);
    }
    auto te = request->getHeader("Transfer-Encoding");
    oatpp::String body = (len || te) ? request->readBodyToString() : oatpp::String("");
    if (body && body->size() > static_cast<size_t>(limit))
      return BaseController::errorJson(m_om, 413, "PAYLOAD_TOO_LARGE", limitText);
    request->putBundleData("cf.body", body ? body : oatpp::String(""));
    return nullptr;
  }

 private:
  std::shared_ptr<oatpp::data::mapping::ObjectMapper> m_om;
};

class SecurityHeadersInterceptor : public oatpp::web::server::interceptor::ResponseInterceptor {
 public:
  std::shared_ptr<OutgoingResponse> intercept(const std::shared_ptr<IncomingRequest>& request,
                                              const std::shared_ptr<OutgoingResponse>& response) override {
    response->putHeaderIfNotExists("X-Content-Type-Options", "nosniff");
    response->putHeaderIfNotExists("X-Frame-Options", "DENY");
    response->putHeaderIfNotExists("Referrer-Policy", "no-referrer");
    // Patient data must never be cached by browsers or intermediaries.
    std::string path = request ? std::string(request->getStartingLine().path.toString()->c_str()) : "";
    if (path.rfind("/swagger", 0) != 0) {
      response->putHeaderIfNotExists("Cache-Control", "no-store");
      response->putHeaderIfNotExists("Pragma", "no-cache");
    }
    return response;
  }
};

}  // namespace cf
