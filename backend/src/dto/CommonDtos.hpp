#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

class ErrorBodyDto : public oatpp::DTO {
  DTO_INIT(ErrorBodyDto, DTO)
  DTO_FIELD(Int32, status);
  DTO_FIELD(String, code);
  DTO_FIELD(String, message);
  DTO_FIELD(Fields<String>, fields);
};

class ErrorDto : public oatpp::DTO {
  DTO_INIT(ErrorDto, DTO)
  DTO_FIELD(Object<ErrorBodyDto>, error);
};

class MessageDto : public oatpp::DTO {
  DTO_INIT(MessageDto, DTO)
  DTO_FIELD(String, message);
};

class ReasonDto : public oatpp::DTO {
  DTO_INIT(ReasonDto, DTO)
  DTO_FIELD(String, reason);
};

class HealthDto : public oatpp::DTO {
  DTO_INIT(HealthDto, DTO)
  DTO_FIELD(String, status);
  DTO_FIELD(String, database);
  DTO_FIELD(String, abdmMode);
  DTO_FIELD(String, version);
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
