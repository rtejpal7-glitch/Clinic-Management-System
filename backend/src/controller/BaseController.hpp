#pragma once
// Shared controller plumbing:
//  - guard(): converts ApiError / DbError / unexpected exceptions into a uniform
//    JSON error envelope and never leaks internal details on 500s
//  - auth(): backend permission enforcement for every protected endpoint
//  - parse<T>(): strict JSON body parsing with 400 on malformed input
//  - query helpers

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>

#include "common/ApiError.hpp"
#include "db/Db.hpp"
#include "dto/CommonDtos.hpp"
#include "oatpp/core/macro/component.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/web/protocol/http/outgoing/BufferBody.hpp"
#include "oatpp/web/server/api/ApiController.hpp"
#include "security/AuthService.hpp"

namespace cf {

class BaseController : public oatpp::web::server::api::ApiController {
 protected:
  using IncomingRequest = oatpp::web::protocol::http::incoming::Request;
  using OutgoingResponse = oatpp::web::protocol::http::outgoing::Response;
  using Status = oatpp::web::protocol::http::Status;

  std::shared_ptr<AuthService> m_auth;
  std::shared_ptr<oatpp::data::mapping::ObjectMapper> m_om;
  bool m_trustProxy = false;

 public:
  BaseController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth,
                 bool trustProxy)
      : ApiController(om), m_auth(std::move(auth)), m_om(om), m_trustProxy(trustProxy) {}

  static Status statusFor(int code) {
    switch (code) {
      case 400: return Status::CODE_400;
      case 401: return Status::CODE_401;
      case 403: return Status::CODE_403;
      case 404: return Status::CODE_404;
      case 405: return Status::CODE_405;
      case 409: return Status::CODE_409;
      case 413: return Status::CODE_413;
      case 422: return Status::CODE_422;
      case 423: return Status::CODE_423;
      case 429: return Status::CODE_429;
      case 503: return Status::CODE_503;
      default: return code >= 500 ? Status::CODE_500 : Status::CODE_400;
    }
  }

  static std::shared_ptr<OutgoingResponse> errorJson(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om,
                                                     int status, const std::string& code, const std::string& message,
                                                     const std::map<std::string, std::string>& fields = {},
                                                     const nlohmann::json& data = nullptr) {
    nlohmann::json j = {{"error", {{"status", status}, {"code", code}, {"message", message}}}};
    if (!fields.empty()) j["error"]["fields"] = fields;
    if (!data.is_null()) j["error"]["data"] = data;
    auto resp = OutgoingResponse::createShared(
        statusFor(status),
        oatpp::web::protocol::http::outgoing::BufferBody::createShared(j.dump(), "application/json"));
    return resp;
  }

 protected:
  RequestMeta meta(const std::shared_ptr<IncomingRequest>& req) const {
    RequestMeta m;
    auto ua = req->getHeader("User-Agent");
    m.userAgent = ua ? std::string(*ua) : "";
    if (m_trustProxy) {
      auto xff = req->getHeader("X-Forwarded-For");
      if (xff) {
        std::string v = *xff;
        m.ip = v.substr(0, v.find(','));
      }
    }
    if (m.ip.empty()) {
      auto& props = req->getConnection()->getInputStreamContext().getProperties();
      auto addr = props.get(oatpp::network::tcp::server::ConnectionProvider::ExtendedConnection::PROPERTY_PEER_ADDRESS);
      m.ip = addr ? std::string(addr->c_str()) : "unknown";
    }
    return m;
  }

  AuthContext auth(const std::shared_ptr<IncomingRequest>& req, const std::string& permission) {
    return m_auth->authenticate(req->getHeader("Authorization"), meta(req), permission);
  }

  static oatpp::String bodyOf(const std::shared_ptr<IncomingRequest>& req) {
    return req->getBundleData<oatpp::String>("cf.body");
  }

  template <class T>
  T parse(const oatpp::String& body) {
    if (!body || body->empty()) throw ApiError::badRequest("Request body is required");
    try {
      auto r = m_om->readFromString<T>(body);
      if (!r) throw ApiError::badRequest("Request body is required");
      return r;
    } catch (const ApiError&) {
      throw;
    } catch (const std::exception&) {
      throw ApiError::badRequest("Malformed JSON request body or wrong field types");
    }
  }

  // Oat++ returns raw query values; decode application/x-www-form-urlencoded ('+' and %XX).
  static std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
      char c = in[i];
      if (c == '+') {
        out += ' ';
      } else if (c == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
        out += static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16));
        i += 2;
      } else {
        out += c;
      }
    }
    return out;
  }

  static std::optional<std::string> q(const std::shared_ptr<IncomingRequest>& req, const char* name) {
    auto v = req->getQueryParameter(name);
    if (!v || v->empty()) return std::nullopt;
    std::string decoded = urlDecode(*v);
    if (decoded.size() > 200) throw ApiError::badRequest(std::string("Query parameter '") + name + "' is too long");
    return decoded;
  }
  static int qInt(const std::shared_ptr<IncomingRequest>& req, const char* name, int def) {
    auto v = q(req, name);
    if (!v) return def;
    try {
      return std::stoi(*v);
    } catch (...) {
      throw ApiError::badRequest(std::string("Query parameter '") + name + "' must be an integer");
    }
  }

  std::shared_ptr<OutgoingResponse> rawJson(const Status& status, const std::string& body,
                                            const char* contentType = "application/json") {
    return OutgoingResponse::createShared(
        status, oatpp::web::protocol::http::outgoing::BufferBody::createShared(body, contentType));
  }

  std::shared_ptr<OutgoingResponse> guard(const std::function<std::shared_ptr<OutgoingResponse>()>& fn) {
    // Tenant context is per request: start empty, always clear on the way out.
    struct ContextReset {
      ContextReset() { DbContext::clear(); }
      ~ContextReset() { DbContext::clear(); }
    } reset;
    try {
      return fn();
    } catch (const ApiError& e) {
      return errorJson(m_om, e.status, e.code, e.what(), e.fields, e.data);
    } catch (const DbError& e) {
      // Map integrity violations that slipped past service validation.
      if (e.isRaisedException()) return errorJson(m_om, 409, "RECORD_LOCKED", e.what());
      if (e.isExclusionViolation()) return errorJson(m_om, 409, "CONFLICT", "The requested time conflicts with an existing booking");
      if (e.isUniqueViolation()) return errorJson(m_om, 409, "DUPLICATE", "A record with the same unique value already exists");
      if (e.isForeignKeyViolation()) return errorJson(m_om, 400, "INVALID_REFERENCE", "Referenced record does not exist");
      if (e.isCheckViolation()) return errorJson(m_om, 400, "VALIDATION_ERROR", "A value is outside the allowed range");
      if (e.sqlstate == "22P02" || e.sqlstate == "22007" || e.sqlstate == "22008")
        return errorJson(m_om, 400, "VALIDATION_ERROR", "Invalid identifier, date or number format");
      OATPP_LOGE("DB", "Unhandled database error [%s]: %s", e.sqlstate.c_str(), e.what());
      return errorJson(m_om, 500, "INTERNAL_ERROR", "An unexpected error occurred");
    } catch (const std::exception& e) {
      OATPP_LOGE("API", "Unhandled exception: %s", e.what());
      return errorJson(m_om, 500, "INTERNAL_ERROR", "An unexpected error occurred");
    }
  }
};

}  // namespace cf

// Swagger helpers
#define CF_DOC(SUMMARY, TAG)        \
  info->summary = SUMMARY;          \
  info->addTag(TAG);                \
  info->authorization = "Bearer";  \
  info->addSecurityRequirement("bearerAuth"); \
  info->addResponse<oatpp::Object<cf::dto::ErrorDto>>(Status::CODE_401, "application/json"); \
  info->addResponse<oatpp::Object<cf::dto::ErrorDto>>(Status::CODE_403, "application/json");
