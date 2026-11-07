#pragma once
#include "BaseController.hpp"
#include "dto/AdminDtos.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class AuthController : public BaseController {
 public:
  using BaseController::BaseController;

  ENDPOINT_INFO(login) {
    info->summary = "Sign in with username and password; returns a bearer token";
    info->addTag("Auth");
    info->addConsumes<Object<dto::LoginRequestDto>>("application/json");
    info->addResponse<Object<dto::LoginResponseDto>>(Status::CODE_200, "application/json");
    info->addResponse<Object<dto::ErrorDto>>(Status::CODE_401, "application/json");
  }
  ENDPOINT("POST", "/api/v1/auth/login", login, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto in = parse<Object<dto::LoginRequestDto>>(bodyOf(request));
      return createDtoResponse(Status::CODE_200,
                               m_auth->login(in->username ? *in->username : "", in->password ? *in->password : "", meta(request)));
    });
  }

  ENDPOINT_INFO(logout) { CF_DOC("Sign out (revokes the current session)", "Auth") }
  ENDPOINT("POST", "/api/v1/auth/logout", logout, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::AUTHENTICATED);
      m_auth->logout(ctx);
      auto m = dto::MessageDto::createShared();
      m->message = "Signed out";
      return createDtoResponse(Status::CODE_200, m);
    });
  }

  ENDPOINT_INFO(me) {
    CF_DOC("Current user, roles and effective permissions", "Auth")
    info->addResponse<Object<dto::CurrentUserDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/auth/me", me, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_auth->currentUser(auth(request, perm::AUTHENTICATED))); });
  }

  ENDPOINT_INFO(changePassword) {
    CF_DOC("Change own password (revokes other sessions)", "Auth")
    info->addConsumes<Object<dto::ChangePasswordDto>>("application/json");
  }
  ENDPOINT("POST", "/api/v1/auth/change-password", changePassword, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::AUTHENTICATED);
      auto in = parse<Object<dto::ChangePasswordDto>>(bodyOf(request));
      m_auth->changePassword(ctx, in->currentPassword ? *in->currentPassword : "", in->newPassword ? *in->newPassword : "");
      auto m = dto::MessageDto::createShared();
      m->message = "Password changed";
      return createDtoResponse(Status::CODE_200, m);
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
