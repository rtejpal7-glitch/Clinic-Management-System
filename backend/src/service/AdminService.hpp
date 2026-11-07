#pragma once
// Users, roles/permissions, clinic settings and dashboard metrics.

#include <memory>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "dto/AdminDtos.hpp"
#include "security/AuthContext.hpp"

namespace cf {

class AdminService {
 public:
  AdminService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit)
      : m_db(std::move(db)), m_audit(std::move(audit)) {}

  oatpp::Vector<oatpp::Object<dto::UserDto>> listUsers() const;
  oatpp::Object<dto::UserDto> createUser(const AuthContext& ctx, const oatpp::Object<dto::UserInputDto>& in);
  oatpp::Object<dto::UserDto> updateUser(const AuthContext& ctx, const std::string& id,
                                         const oatpp::Object<dto::UserInputDto>& in);
  void resetPassword(const AuthContext& ctx, const std::string& id, const std::string& newPassword);

  oatpp::Vector<oatpp::Object<dto::RoleDto>> listRoles() const;
  oatpp::Vector<oatpp::Object<dto::PermissionDto>> listPermissions() const;
  oatpp::Object<dto::RoleDto> createRole(const AuthContext& ctx, const oatpp::Object<dto::RoleInputDto>& in);
  oatpp::Object<dto::RoleDto> updateRole(const AuthContext& ctx, const std::string& id,
                                         const oatpp::Object<dto::RoleInputDto>& in);

  oatpp::Object<dto::SettingsDto> getSettings() const;
  oatpp::Object<dto::SettingsDto> updateSettings(const AuthContext& ctx, const oatpp::Object<dto::SettingsDto>& in);

  oatpp::Object<dto::DashboardDto> dashboard(const AuthContext& ctx) const;

 private:
  oatpp::Object<dto::UserDto> getUser(Conn& c, const std::string& id) const;
  oatpp::Object<dto::RoleDto> getRole(Conn& c, const std::string& id) const;
  void setUserRoles(Conn& c, const std::string& userId, const oatpp::Vector<oatpp::String>& roles) const;
  void ensureAdminRemains(Conn& c) const;
  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
};

}  // namespace cf
