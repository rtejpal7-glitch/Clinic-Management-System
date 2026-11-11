#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

class FacilityDto : public oatpp::DTO {
  DTO_INIT(FacilityDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, code);
  DTO_FIELD(String, name);
  DTO_FIELD(String, specialty);
  DTO_FIELD(String, city);
  DTO_FIELD(String, state);
  DTO_FIELD(String, hfrId);
  DTO_FIELD(Boolean, isActive);
  DTO_FIELD(Boolean, networkSharingEnabled);
  DTO_FIELD(String, networkAgreementRef);
  DTO_FIELD(String, networkEnabledAt);
  DTO_FIELD(Int64, userCount);
  DTO_FIELD(String, createdAt);
};

class FacilityCreateDto : public oatpp::DTO {
  DTO_INIT(FacilityCreateDto, DTO)
  DTO_FIELD(String, code);
  DTO_FIELD(String, name);
  DTO_FIELD(String, specialty);
  DTO_FIELD(String, city);
  DTO_FIELD(String, state);
  DTO_FIELD(String, pincode);
  DTO_FIELD(String, phone);
  // First administrator of the new clinic (must change password on first sign-in)
  DTO_FIELD(String, adminUsername);
  DTO_FIELD(String, adminFullName);
  DTO_FIELD(String, adminPassword);
};

class NetworkSharingDto : public oatpp::DTO {
  DTO_INIT(NetworkSharingDto, DTO)
  DTO_FIELD(Boolean, enabled);
  DTO_FIELD(String, agreementRef);  // data-sharing agreement / governance approval reference
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
