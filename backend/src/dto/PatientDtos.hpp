#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

class PatientInputDto : public oatpp::DTO {
  DTO_INIT(PatientInputDto, DTO)
  DTO_FIELD(String, fullName);
  DTO_FIELD(String, dateOfBirth);          // YYYY-MM-DD
  DTO_FIELD(Boolean, dobIsEstimated);
  DTO_FIELD(String, gender);               // MALE | FEMALE | OTHER | UNDISCLOSED
  DTO_FIELD(String, mobile);               // 10-digit Indian mobile
  DTO_FIELD(String, email);
  DTO_FIELD(String, addressLine);
  DTO_FIELD(String, city);
  DTO_FIELD(String, district);
  DTO_FIELD(String, state);
  DTO_FIELD(String, pincode);
  DTO_FIELD(String, emergencyContactName);
  DTO_FIELD(String, emergencyContactRelation);
  DTO_FIELD(String, emergencyContactPhone);
  DTO_FIELD(String, bloodGroup);
  DTO_FIELD(String, maritalStatus);
  DTO_FIELD(String, occupation);
  DTO_FIELD(String, preferredLanguage);
  // Registration only: acknowledge the duplicate warning explicitly
  DTO_FIELD(Boolean, confirmNotDuplicate);
  DTO_FIELD(String, duplicateOverrideReason);
  // Update only: optimistic locking + reason for change history
  DTO_FIELD(Int32, version);
  DTO_FIELD(String, changeReason);
};

class PatientDto : public oatpp::DTO {
  DTO_INIT(PatientDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, patientNumber);
  DTO_FIELD(String, fullName);
  DTO_FIELD(String, dateOfBirth);
  DTO_FIELD(Boolean, dobIsEstimated);
  DTO_FIELD(Int32, ageYears);
  DTO_FIELD(String, gender);
  DTO_FIELD(String, mobile);
  DTO_FIELD(String, email);
  DTO_FIELD(String, addressLine);
  DTO_FIELD(String, city);
  DTO_FIELD(String, district);
  DTO_FIELD(String, state);
  DTO_FIELD(String, pincode);
  DTO_FIELD(String, emergencyContactName);
  DTO_FIELD(String, emergencyContactRelation);
  DTO_FIELD(String, emergencyContactPhone);
  DTO_FIELD(String, bloodGroup);
  DTO_FIELD(String, maritalStatus);
  DTO_FIELD(String, occupation);
  DTO_FIELD(String, preferredLanguage);
  DTO_FIELD(Boolean, isActive);
  DTO_FIELD(Boolean, abhaLinked);
  DTO_FIELD(Int32, version);
  DTO_FIELD(String, createdAt);
  DTO_FIELD(String, updatedAt);
};

class PatientPageDto : public oatpp::DTO {
  DTO_INIT(PatientPageDto, DTO)
  DTO_FIELD(Vector<Object<PatientDto>>, items);
  DTO_FIELD(Int64, total);
  DTO_FIELD(Int32, page);
  DTO_FIELD(Int32, size);
};

class DuplicateCandidateDto : public oatpp::DTO {
  DTO_INIT(DuplicateCandidateDto, DTO)
  DTO_FIELD(Object<PatientDto>, patient);
  DTO_FIELD(Int32, score);
  DTO_FIELD(Vector<String>, matchedOn);
};

class DuplicateCheckResultDto : public oatpp::DTO {
  DTO_INIT(DuplicateCheckResultDto, DTO)
  DTO_FIELD(Boolean, possibleDuplicates);
  DTO_FIELD(Int32, threshold);
  DTO_FIELD(Vector<Object<DuplicateCandidateDto>>, candidates);
};

class PatientChangeDto : public oatpp::DTO {
  DTO_INIT(PatientChangeDto, DTO)
  DTO_FIELD(Int64, id);
  DTO_FIELD(String, changedAt);
  DTO_FIELD(String, changedBy);
  DTO_FIELD(String, fieldName);
  DTO_FIELD(String, oldValue);
  DTO_FIELD(String, newValue);
  DTO_FIELD(String, reason);
};

class AllergyDto : public oatpp::DTO {
  DTO_INIT(AllergyDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, substance);
  DTO_FIELD(String, category);
  DTO_FIELD(String, reaction);
  DTO_FIELD(String, severity);
  DTO_FIELD(String, status);
  DTO_FIELD(String, recordedBy);
  DTO_FIELD(String, createdAt);
};

class AllergyInputDto : public oatpp::DTO {
  DTO_INIT(AllergyInputDto, DTO)
  DTO_FIELD(String, substance);
  DTO_FIELD(String, category);
  DTO_FIELD(String, reaction);
  DTO_FIELD(String, severity);
  DTO_FIELD(String, status);
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
