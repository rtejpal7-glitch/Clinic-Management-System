#pragma once
#include "BaseController.hpp"
#include "abdm/AbdmService.hpp"
#include "dto/AbdmDtos.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)
namespace cf::dto {
class MockFailureDto : public oatpp::DTO {
  DTO_INIT(MockFailureDto, DTO)
  DTO_FIELD(String, operation);
  DTO_FIELD(Int32, count);
  DTO_FIELD(Boolean, retryable);
};
}  // namespace cf::dto
#include OATPP_CODEGEN_END(DTO)

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class AbdmController : public BaseController {
  std::shared_ptr<AbdmService> m_svc;

 public:
  AbdmController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth, bool tp,
                 std::shared_ptr<AbdmService> svc)
      : BaseController(om, std::move(auth), tp), m_svc(std::move(svc)) {}

  ENDPOINT_INFO(status) {
    CF_DOC("ABDM integration mode (mock) and supported consent parameters", "ABDM (mock)")
    info->addResponse<Object<dto::AbdmStatusDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/abdm/status", status, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::AUTHENTICATED);
      return createDtoResponse(Status::CODE_200, m_svc->status());
    });
  }

  ENDPOINT_INFO(identifiers) {
    CF_DOC("ABHA identifiers linked to a patient (masked)", "ABDM (mock)")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::IdentifierDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}/abha", identifiers, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::ABDM_READ);
      return createDtoResponse(Status::CODE_200, m_svc->identifiers(id));
    });
  }

  ENDPOINT_INFO(verifyInit) {
    CF_DOC("Start ABHA linking: gateway sends an OTP to the patient (mock: no OTP is sent)", "ABDM (mock)")
    info->addConsumes<Object<dto::AbhaVerifyInitDto>>("application/json");
    info->addResponse<Object<dto::AbdmTransactionDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/patients/{id}/abha/verify-init", verifyInit, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ABDM_LINK);
      return createDtoResponse(Status::CODE_200, m_svc->initAbhaVerification(ctx, id, parse<Object<dto::AbhaVerifyInitDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(verifyConfirm) {
    CF_DOC("Confirm ABHA linking with the OTP (mock: 123456 succeeds, 999999 simulates timeout)", "ABDM (mock)")
    info->addConsumes<Object<dto::AbhaVerifyConfirmDto>>("application/json");
    info->addResponse<Object<dto::AbdmTransactionDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/abdm/transactions/{id}/verify-confirm", verifyConfirm, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ABDM_LINK);
      auto in = parse<Object<dto::AbhaVerifyConfirmDto>>(bodyOf(request));
      return createDtoResponse(Status::CODE_200, m_svc->confirmAbhaVerification(ctx, id, in->otp ? *in->otp : ""));
    });
  }

  ENDPOINT_INFO(unlink) {
    CF_DOC("Unlink an ABHA identifier from the patient record", "ABDM (mock)")
    info->addConsumes<Object<dto::ReasonDto>>("application/json");
    info->addResponse<Object<dto::IdentifierDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/patients/{id}/abha/{identifierId}/unlink", unlink, PATH(String, id), PATH(String, identifierId),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ABDM_LINK);
      auto in = parse<Object<dto::ReasonDto>>(bodyOf(request));
      return createDtoResponse(Status::CODE_200, m_svc->unlink(ctx, id, identifierId, in->reason ? *in->reason : ""));
    });
  }

  ENDPOINT_INFO(consents) {
    CF_DOC("Consent records for a patient", "ABDM (mock)")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::ConsentDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}/consents", consents, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::ABDM_READ);
      return createDtoResponse(Status::CODE_200, m_svc->consents(id));
    });
  }

  ENDPOINT_INFO(requestConsent) {
    CF_DOC("Raise a consent request (requires a verified ABHA link)", "ABDM (mock)")
    info->addConsumes<Object<dto::ConsentRequestDto>>("application/json");
    info->addResponse<Object<dto::ConsentDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/patients/{id}/consents", requestConsent, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ABDM_CONSENT);
      return createDtoResponse(Status::CODE_201, m_svc->requestConsent(ctx, id, parse<Object<dto::ConsentRequestDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(mockRespond) {
    CF_DOC("MOCK ONLY: simulate the patient granting or denying a consent request in their PHR app", "ABDM (mock)")
    info->addConsumes<Object<dto::MockConsentResponseDto>>("application/json");
    info->addResponse<Object<dto::ConsentDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/consents/{id}/mock-respond", mockRespond, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ABDM_CONSENT);
      auto in = parse<Object<dto::MockConsentResponseDto>>(bodyOf(request));
      return createDtoResponse(Status::CODE_200, m_svc->mockRespond(ctx, id, in->decision ? *in->decision : ""));
    });
  }

  ENDPOINT_INFO(revoke) {
    CF_DOC("Record withdrawal / revocation of consent", "ABDM (mock)")
    info->addConsumes<Object<dto::ReasonDto>>("application/json");
    info->addResponse<Object<dto::ConsentDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/consents/{id}/revoke", revoke, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ABDM_CONSENT);
      auto in = parse<Object<dto::ReasonDto>>(bodyOf(request));
      return createDtoResponse(Status::CODE_200, m_svc->revoke(ctx, id, in->reason ? *in->reason : ""));
    });
  }

  ENDPOINT_INFO(share) {
    CF_DOC("Share a finalized consultation as an FHIR document bundle against a granted consent (mock push)", "ABDM (mock)")
    info->addResponse<Object<dto::ShareResultDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/consultations/{id}/abdm-share", share, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_svc->shareConsultation(auth(request, perm::FHIR_EXPORT), id)); });
  }

  ENDPOINT_INFO(transactions) {
    CF_DOC("ABDM transaction log (optional patientId)", "ABDM (mock)")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::AbdmTransactionDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/abdm/transactions", transactions, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::ABDM_READ);
      return createDtoResponse(Status::CODE_200, m_svc->transactions(q(request, "patientId")));
    });
  }

  ENDPOINT_INFO(retry) {
    CF_DOC("Retry a failed consent request or health-information share", "ABDM (mock)")
    info->addResponse<Object<dto::AbdmTransactionDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/abdm/transactions/{id}/retry", retry, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_svc->retry(auth(request, perm::ABDM_CONSENT), id)); });
  }

  ENDPOINT_INFO(injectFailure) {
    CF_DOC("MOCK ONLY: make the next N gateway calls of an operation fail (for testing/demo)", "ABDM (mock)")
    info->addConsumes<Object<dto::MockFailureDto>>("application/json");
  }
  ENDPOINT("POST", "/api/v1/abdm/mock/inject-failure", injectFailure, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::ABDM_CONSENT);
      auto in = parse<Object<dto::MockFailureDto>>(bodyOf(request));
      m_svc->injectMockFailure(ctx, in->operation ? std::string(*in->operation) : "", in->count ? *in->count : 1,
                               in->retryable ? *in->retryable : true);
      auto m = dto::MessageDto::createShared();
      m->message = "Failure injection armed (mock gateway)";
      return createDtoResponse(Status::CODE_200, m);
    });
  }

  // ---------- FHIR ----------
  ENDPOINT_INFO(fhir) {
    CF_DOC("Export a record as FHIR R4 JSON. type: Patient | Practitioner | Organization | Appointment | Encounter | Invoice | "
           "Bundle (consultation document)", "FHIR")
  }
  ENDPOINT("GET", "/api/v1/fhir/{type}/{id}", fhir, PATH(String, type), PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::FHIR_EXPORT);
      auto res = m_svc->fhirExport(ctx, type, id);
      return rawJson(Status::CODE_200, res.dump(2), "application/fhir+json");
    });
  }

  ENDPOINT_INFO(fhirValidate) {
    CF_DOC("Local structural validation of the consultation FHIR bundle", "FHIR")
  }
  ENDPOINT("GET", "/api/v1/fhir/Bundle/{id}/validate", fhirValidate, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::FHIR_EXPORT);
      auto bundle = m_svc->fhirExport(ctx, "Bundle", id);
      auto issues = FhirMapper::validateBundle(bundle);
      nlohmann::json out = {{"valid", issues.empty()}, {"issues", issues}, {"entries", bundle["entry"].size()},
                            {"scope", "Local structural checks only. Validate against the NRCeS ABDM FHIR IG with the official HL7 validator before production use."}};
      return rawJson(Status::CODE_200, out.dump());
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
