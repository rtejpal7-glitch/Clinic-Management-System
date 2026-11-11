#pragma once
#include "BaseController.hpp"
#include "dto/FacilityDtos.hpp"
#include "service/FacilityService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class FacilityController : public BaseController {
  std::shared_ptr<FacilityService> m_facilities;

 public:
  FacilityController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth,
                     bool trustProxy, std::shared_ptr<FacilityService> facilities)
      : BaseController(om, std::move(auth), trustProxy), m_facilities(std::move(facilities)) {}

  ENDPOINT_INFO(listFacilities) {
    CF_DOC("Clinics on this installation (platform admin: all; others: own clinic)", "Clinics")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::FacilityDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/facilities", listFacilities, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_facilities->list(auth(request, perm::AUTHENTICATED))); });
  }

  ENDPOINT_INFO(currentFacility) {
    CF_DOC("The signed-in user's clinic", "Clinics")
    info->addResponse<Object<dto::FacilityDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/facilities/current", currentFacility, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_facilities->current(auth(request, perm::AUTHENTICATED))); });
  }

  ENDPOINT_INFO(createFacility) {
    CF_DOC("Create a clinic with its first administrator (platform administrators only)", "Clinics")
    info->addConsumes<Object<dto::FacilityCreateDto>>("application/json");
    info->addResponse<Object<dto::FacilityDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/facilities", createFacility, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PLATFORM_MANAGE);
      return createDtoResponse(Status::CODE_201,
                               m_facilities->create(ctx, parse<Object<dto::FacilityCreateDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(networkSharing) {
    CF_DOC("Join or leave the de-identified ClinicFlow prescribing network (requires an agreement reference)", "Clinics")
    info->addConsumes<Object<dto::NetworkSharingDto>>("application/json");
    info->addResponse<Object<dto::FacilityDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/facilities/current/network", networkSharing, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::SETTINGS_MANAGE);
      return createDtoResponse(Status::CODE_200,
                               m_facilities->setNetworkSharing(ctx, parse<Object<dto::NetworkSharingDto>>(bodyOf(request))));
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
