#pragma once
// Authentication (login/logout/sessions) and authorization (permission checks).
//  - Argon2id password verification with a dummy hash for unknown users (timing-safe-ish)
//  - Account lockout after repeated failures
//  - Opaque random bearer tokens; only SHA-256(token) is stored server-side
//  - Idle timeout (clinic setting) + absolute session lifetime
//  - Every denial and auth event is written to the audit log

#include <memory>
#include <string>

#include "audit/AuditService.hpp"
#include "config/Config.hpp"
#include "db/Db.hpp"
#include "dto/AdminDtos.hpp"
#include "security/AuthContext.hpp"

namespace cf {

class AuthService {
 public:
  AuthService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit, const Config& cfg)
      : m_db(std::move(db)), m_audit(std::move(audit)), m_absoluteHours(cfg.sessionAbsoluteHours) {}

  oatpp::Object<dto::LoginResponseDto> login(const std::string& username, const std::string& password,
                                             const RequestMeta& meta);
  void logout(const AuthContext& ctx);

  // Resolve the bearer token and require a permission (empty = any authenticated user).
  AuthContext authenticate(const oatpp::String& authorizationHeader, const RequestMeta& meta,
                           const std::string& requiredPermission);
  void require(const AuthContext& ctx, const std::string& permission, const std::string& resource = "") const;

  oatpp::Object<dto::CurrentUserDto> currentUser(const AuthContext& ctx) const;
  void changePassword(const AuthContext& ctx, const std::string& current, const std::string& next);

  static void validatePasswordPolicy(const std::string& password, const std::string& username);
  void bootstrapAdmin(const Config& cfg);

  static const int MAX_FAILED_ATTEMPTS = 5;
  static const int LOCKOUT_MINUTES = 15;

 private:
  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
  int m_absoluteHours;
};

}  // namespace cf
