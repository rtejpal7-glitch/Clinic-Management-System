#pragma once
// Application error type. Services throw ApiError; controllers convert it into
// a consistent JSON error envelope: {"error": {"status", "code", "message", "fields"}}.

#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace cf {

class ApiError : public std::runtime_error {
 public:
  int status;
  std::string code;
  std::map<std::string, std::string> fields;  // field-level validation messages
  nlohmann::json data;                        // optional structured payload (e.g. duplicate candidates)

  ApiError(int status, std::string code, const std::string& message,
           std::map<std::string, std::string> fields = {})
      : std::runtime_error(message), status(status), code(std::move(code)), fields(std::move(fields)) {}

  static ApiError badRequest(const std::string& msg, std::map<std::string, std::string> f = {}) {
    return ApiError(400, "VALIDATION_ERROR", msg, std::move(f));
  }
  static ApiError unauthorized(const std::string& msg = "Authentication required") {
    return ApiError(401, "UNAUTHENTICATED", msg);
  }
  static ApiError forbidden(const std::string& msg = "You do not have permission to perform this action") {
    return ApiError(403, "FORBIDDEN", msg);
  }
  static ApiError notFound(const std::string& what) { return ApiError(404, "NOT_FOUND", what + " not found"); }
  static ApiError conflict(const std::string& code, const std::string& msg) { return ApiError(409, code, msg); }
  static ApiError withData(int status, const std::string& code, const std::string& msg, nlohmann::json d) {
    ApiError e(status, code, msg);
    e.data = std::move(d);
    return e;
  }
  static ApiError unprocessable(const std::string& code, const std::string& msg) { return ApiError(422, code, msg); }
};

// Collects field validation errors and throws once with all of them.
class Validator {
 public:
  std::map<std::string, std::string> errors;
  void fail(const std::string& field, const std::string& msg) {
    if (!errors.count(field)) errors[field] = msg;
  }
  void check(bool ok, const std::string& field, const std::string& msg) {
    if (!ok) fail(field, msg);
  }
  void throwIfAny(const std::string& msg = "Please correct the highlighted fields") const {
    if (!errors.empty()) throw ApiError::badRequest(msg, errors);
  }
};

}  // namespace cf
