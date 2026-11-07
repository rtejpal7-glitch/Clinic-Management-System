#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

class AbdmStatusDto : public oatpp::DTO {
  DTO_INIT(AbdmStatusDto, DTO)
  DTO_FIELD(String, mode);          // mock
  DTO_FIELD(Boolean, isMock);
  DTO_FIELD(String, notice);
  DTO_FIELD(String, hfrId);
  DTO_FIELD(Vector<String>, supportedHiTypes);
  DTO_FIELD(Vector<String>, supportedPurposes);
};

class IdentifierDto : public oatpp::DTO {
  DTO_INIT(IdentifierDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, idType);
  DTO_FIELD(String, valueMasked);
  DTO_FIELD(String, verificationStatus);
  DTO_FIELD(String, verificationMethod);
  DTO_FIELD(String, verifiedAt);
  DTO_FIELD(Boolean, isMock);
  DTO_FIELD(String, unlinkedAt);
  DTO_FIELD(String, unlinkReason);
  DTO_FIELD(String, createdAt);
};

class AbhaVerifyInitDto : public oatpp::DTO {
  DTO_INIT(AbhaVerifyInitDto, DTO)
  DTO_FIELD(String, idType);   // ABHA_NUMBER | ABHA_ADDRESS
  DTO_FIELD(String, value);
  DTO_FIELD(String, authMethod);  // MOBILE_OTP | AADHAAR_OTP (as offered by ABDM)
  DTO_FIELD(Boolean, patientConsented);  // patient agreed to ABHA linking at the counter
};

class AbhaVerifyConfirmDto : public oatpp::DTO {
  DTO_INIT(AbhaVerifyConfirmDto, DTO)
  DTO_FIELD(String, otp);
};

class AbdmTransactionDto : public oatpp::DTO {
  DTO_INIT(AbdmTransactionDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, requestId);
  DTO_FIELD(String, txnType);
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, consentId);
  DTO_FIELD(String, identifierId);
  DTO_FIELD(String, status);
  DTO_FIELD(Int32, attemptCount);
  DTO_FIELD(Int32, maxAttempts);
  DTO_FIELD(String, nextRetryAt);
  DTO_FIELD(String, gatewayTxnId);
  DTO_FIELD(String, errorCode);
  DTO_FIELD(String, errorMessage);
  DTO_FIELD(String, responseSummary);
  DTO_FIELD(Boolean, isMock);
  DTO_FIELD(String, createdAt);
  DTO_FIELD(String, updatedAt);
};

class ConsentDto : public oatpp::DTO {
  DTO_INIT(ConsentDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, direction);
  DTO_FIELD(String, purposeCode);
  DTO_FIELD(Vector<String>, hiTypes);
  DTO_FIELD(String, dateFrom);
  DTO_FIELD(String, dateTo);
  DTO_FIELD(String, expiresAt);
  DTO_FIELD(String, status);
  DTO_FIELD(String, statusReason);
  DTO_FIELD(String, consentRequestRef);
  DTO_FIELD(String, consentArtefactRef);
  DTO_FIELD(String, requestedBy);
  DTO_FIELD(String, requestedAt);
  DTO_FIELD(String, respondedAt);
  DTO_FIELD(String, revokedAt);
  DTO_FIELD(Boolean, isMock);
};

class ConsentRequestDto : public oatpp::DTO {
  DTO_INIT(ConsentRequestDto, DTO)
  DTO_FIELD(String, direction);         // HIU_REQUEST | HIP_SHARE
  DTO_FIELD(String, purposeCode);
  DTO_FIELD(Vector<String>, hiTypes);
  DTO_FIELD(String, dateFrom);
  DTO_FIELD(String, dateTo);
  DTO_FIELD(String, expiresAt);
};

class MockConsentResponseDto : public oatpp::DTO {
  DTO_INIT(MockConsentResponseDto, DTO)
  DTO_FIELD(String, decision);   // GRANT | DENY
};

class ShareResultDto : public oatpp::DTO {
  DTO_INIT(ShareResultDto, DTO)
  DTO_FIELD(Object<AbdmTransactionDto>, transaction);
  DTO_FIELD(String, careContextReference);
  DTO_FIELD(Int32, resourceCount);
  DTO_FIELD(Vector<String>, validationIssues);
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
