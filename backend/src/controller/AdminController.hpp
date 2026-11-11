#pragma once
#include "BaseController.hpp"
#include "dto/AdminDtos.hpp"
#include "service/AdminService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class AdminController : public BaseController {
  std::shared_ptr<AdminService> m_admin;
  std::shared_ptr<AuditService> m_audit;
  std::shared_ptr<Db> m_db;
  std::string m_abdmMode;

 public:
  AdminController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth,
                  bool trustProxy, std::shared_ptr<AdminService> admin, std::shared_ptr<AuditService> audit,
                  std::shared_ptr<Db> db, std::string abdmMode)
      : BaseController(om, std::move(auth), trustProxy), m_admin(std::move(admin)), m_audit(std::move(audit)),
        m_db(std::move(db)), m_abdmMode(std::move(abdmMode)) {}

  ENDPOINT_INFO(health) {
    info->summary = "Liveness / readiness probe";
    info->addTag("System");
    info->addResponse<Object<dto::HealthDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/health", health) {
    return guard([&] {
      auto h = dto::HealthDto::createShared();
      h->status = "ok";
      h->version = "0.1.0";
      h->abdmMode = m_abdmMode;
      try {
        m_db->withConn([](Conn& c) { c.exec("SELECT 1"); });
        h->database = "up";
      } catch (...) {
        h->status = "degraded";
        h->database = "down";
        return createDtoResponse(Status::CODE_503, h);
      }
      return createDtoResponse(Status::CODE_200, h);
    });
  }

  // ---------- Dashboard ----------
  ENDPOINT_INFO(dashboard) {
    CF_DOC("Operational dashboard metrics (financial figures only with billing permissions)", "Dashboard")
    info->addResponse<Object<dto::DashboardDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/dashboard/summary", dashboard, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_admin->dashboard(auth(request, perm::DASHBOARD_READ))); });
  }

  // ---------- Users ----------
  ENDPOINT_INFO(listUsers) {
    CF_DOC("List users", "Users & Roles")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::UserDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/users", listUsers, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::USER_MANAGE);
      return createDtoResponse(Status::CODE_200, m_admin->listUsers());
    });
  }

  ENDPOINT_INFO(createUser) {
    CF_DOC("Create a user (must change password on first sign-in)", "Users & Roles")
    info->addConsumes<Object<dto::UserInputDto>>("application/json");
    info->addResponse<Object<dto::UserDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/users", createUser, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::USER_MANAGE);
      return createDtoResponse(Status::CODE_201, m_admin->createUser(ctx, parse<Object<dto::UserInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(updateUser) {
    CF_DOC("Update user profile, roles or active status", "Users & Roles")
    info->addConsumes<Object<dto::UserInputDto>>("application/json");
    info->addResponse<Object<dto::UserDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/users/{id}", updateUser, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::USER_MANAGE);
      return createDtoResponse(Status::CODE_200, m_admin->updateUser(ctx, id, parse<Object<dto::UserInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(resetPassword) {
    CF_DOC("Reset a user's password (revokes their sessions)", "Users & Roles")
    info->addConsumes<Object<dto::ResetPasswordDto>>("application/json");
  }
  ENDPOINT("POST", "/api/v1/users/{id}/reset-password", resetPassword, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::USER_MANAGE);
      auto in = parse<Object<dto::ResetPasswordDto>>(bodyOf(request));
      m_admin->resetPassword(ctx, id, in->newPassword ? *in->newPassword : "");
      auto m = dto::MessageDto::createShared();
      m->message = "Password reset; user must change it at next sign-in";
      return createDtoResponse(Status::CODE_200, m);
    });
  }

  // ---------- Roles ----------
  ENDPOINT_INFO(listRoles) {
    CF_DOC("List roles with their permissions", "Users & Roles")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::RoleDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/roles", listRoles, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::AUTHENTICATED);
      if (!ctx.has(perm::USER_MANAGE)) m_auth->require(ctx, perm::ROLE_MANAGE, "roles");
      return createDtoResponse(Status::CODE_200, m_admin->listRoles());
    });
  }

  ENDPOINT_INFO(listPermissions) {
    CF_DOC("List all permission codes", "Users & Roles")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::PermissionDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/permissions", listPermissions, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::ROLE_MANAGE);
      return createDtoResponse(Status::CODE_200, m_admin->listPermissions());
    });
  }

  ENDPOINT_INFO(createRole) {
    CF_DOC("Create a custom role", "Users & Roles")
    info->addConsumes<Object<dto::RoleInputDto>>("application/json");
    info->addResponse<Object<dto::RoleDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/roles", createRole, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ROLE_MANAGE);
      return createDtoResponse(Status::CODE_201, m_admin->createRole(ctx, parse<Object<dto::RoleInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(updateRole) {
    CF_DOC("Update role name/description and permission mapping", "Users & Roles")
    info->addConsumes<Object<dto::RoleInputDto>>("application/json");
    info->addResponse<Object<dto::RoleDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/roles/{id}", updateRole, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ROLE_MANAGE);
      return createDtoResponse(Status::CODE_200, m_admin->updateRole(ctx, id, parse<Object<dto::RoleInputDto>>(bodyOf(request))));
    });
  }

  // ---------- Settings ----------
  ENDPOINT_INFO(getSettings) {
    CF_DOC("Clinic configuration", "Settings")
    info->addResponse<Object<dto::SettingsDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/settings", getSettings, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::AUTHENTICATED);
      return createDtoResponse(Status::CODE_200, m_admin->getSettings());
    });
  }

  ENDPOINT_INFO(updateSettings) {
    CF_DOC("Update clinic configuration", "Settings")
    info->addConsumes<Object<dto::SettingsDto>>("application/json");
    info->addResponse<Object<dto::SettingsDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/settings", updateSettings, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::SETTINGS_MANAGE);
      return createDtoResponse(Status::CODE_200, m_admin->updateSettings(ctx, parse<Object<dto::SettingsDto>>(bodyOf(request))));
    });
  }

  // ---------- Audit ----------
  ENDPOINT_INFO(auditLogs) {
    CF_DOC("Search audit log (filters: from, to, userId, action, entityType, entityId, patientId, outcome, page, size)", "Audit")
    info->addResponse<Object<dto::AuditPageDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/audit-logs", auditLogs, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::AUDIT_READ);
      AuditQuery aq;
      aq.from = q(request, "from");
      aq.to = q(request, "to");
      aq.userId = q(request, "userId");
      aq.action = q(request, "action");
      aq.entityType = q(request, "entityType");
      aq.entityId = q(request, "entityId");
      aq.patientId = q(request, "patientId");
      aq.outcome = q(request, "outcome");
      aq.page = qInt(request, "page", 1);
      aq.size = qInt(request, "size", 50);
      aq.includeInstallationLevel = ctx.has(perm::PLATFORM_MANAGE);
      return createDtoResponse(Status::CODE_200, m_audit->list(aq));
    });
  }

  ENDPOINT_INFO(auditVerify) {
    CF_DOC("Verify the audit hash chain (tamper evidence)", "Audit")
    info->addResponse<Object<dto::AuditVerifyDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/audit-logs/verify", auditVerify, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::AUDIT_READ);
      auto result = m_audit->verify();
      m_audit->recordStandalone(AuditActor::from(ctx), AuditEvent{"AUDIT_VERIFY", "audit_logs", "", std::nullopt, "SUCCESS",
                                                                  {{"valid", (bool)*result->valid}}});
      return createDtoResponse(Status::CODE_200, result);
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
