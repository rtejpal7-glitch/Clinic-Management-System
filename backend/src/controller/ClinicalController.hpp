#pragma once
#include "BaseController.hpp"
#include "dto/ClinicalDtos.hpp"
#include "service/ClinicalService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class ClinicalController : public BaseController {
  std::shared_ptr<ClinicalService> m_svc;

 public:
  ClinicalController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth, bool tp,
                     std::shared_ptr<ClinicalService> svc)
      : BaseController(om, std::move(auth), tp), m_svc(std::move(svc)) {}

  ENDPOINT_INFO(start) {
    CF_DOC("Start (or resume) the consultation for a checked-in appointment — treating doctor only", "Consultations")
    info->addResponse<Object<dto::ConsultationDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/appointments/{id}/start-consultation", start, PATH(String, id),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_svc->startFromAppointment(auth(request, perm::CLINICAL_WRITE), id)); });
  }

  ENDPOINT_INFO(get) {
    CF_DOC("Full consultation record with vitals, diagnoses, prescription, orders and addenda (audit-logged)", "Consultations")
    info->addResponse<Object<dto::ConsultationDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/consultations/{id}", get, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_svc->get(auth(request, perm::CLINICAL_READ), id)); });
  }

  ENDPOINT_INFO(update) {
    CF_DOC("Save draft consultation notes, vitals and diagnoses (version required)", "Consultations")
    info->addConsumes<Object<dto::ConsultationUpdateDto>>("application/json");
    info->addResponse<Object<dto::ConsultationDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/consultations/{id}", update, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return createDtoResponse(Status::CODE_200, m_svc->updateDraft(ctx, id, parse<Object<dto::ConsultationUpdateDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(prescription) {
    CF_DOC("Save draft prescription items, advice and investigation orders (CPOE)", "Consultations")
    info->addConsumes<Object<dto::PrescriptionUpdateDto>>("application/json");
    info->addResponse<Object<dto::ConsultationDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/consultations/{id}/prescription", prescription, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PRESCRIPTION_WRITE);
      return createDtoResponse(Status::CODE_200, m_svc->updatePrescription(ctx, id, parse<Object<dto::PrescriptionUpdateDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(finalize) {
    CF_DOC("Finalize (lock) the consultation and prescription; completes the appointment", "Consultations")
    info->addResponse<Object<dto::ConsultationDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/consultations/{id}/finalize", finalize, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_svc->finalize(auth(request, perm::CLINICAL_FINALIZE), id)); });
  }

  ENDPOINT_INFO(addendum) {
    CF_DOC("Add a correction/addendum to a finalized consultation (original is never modified)", "Consultations")
    info->addConsumes<Object<dto::AddendumInputDto>>("application/json");
    info->addResponse<Object<dto::ConsultationDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/consultations/{id}/addenda", addendum, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_FINALIZE);
      return createDtoResponse(Status::CODE_201, m_svc->addAddendum(ctx, id, parse<Object<dto::AddendumInputDto>>(bodyOf(request))));
    });
  }

  // ---------- Catalogues ----------
  ENDPOINT_INFO(medications) {
    CF_DOC("Search the medicine catalogue (q, includeInactive)", "Catalogues")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::MedicationDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/catalogue/medications", medications, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::CATALOGUE_READ);
      return createDtoResponse(Status::CODE_200, m_svc->searchMedications(q(request, "q").value_or(""),
                                                                          q(request, "includeInactive").value_or("") == "true"));
    });
  }

  ENDPOINT_INFO(createMedication) {
    CF_DOC("Add a medicine to the catalogue", "Catalogues")
    info->addConsumes<Object<dto::MedicationDto>>("application/json");
    info->addResponse<Object<dto::MedicationDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/catalogue/medications", createMedication, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CATALOGUE_MANAGE);
      return createDtoResponse(Status::CODE_201, m_svc->saveMedication(ctx, "", parse<Object<dto::MedicationDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(updateMedication) {
    CF_DOC("Edit or deactivate a catalogue medicine", "Catalogues")
    info->addConsumes<Object<dto::MedicationDto>>("application/json");
    info->addResponse<Object<dto::MedicationDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/catalogue/medications/{id}", updateMedication, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CATALOGUE_MANAGE);
      return createDtoResponse(Status::CODE_200, m_svc->saveMedication(ctx, id, parse<Object<dto::MedicationDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(investigations) {
    CF_DOC("Search the investigation catalogue (LOINC-coded where available)", "Catalogues")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::InvestigationCatalogueDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/catalogue/investigations", investigations, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::CATALOGUE_READ);
      return createDtoResponse(Status::CODE_200, m_svc->searchInvestigations(q(request, "q").value_or("")));
    });
  }

  ENDPOINT_INFO(diagnoses) {
    CF_DOC("Search diagnosis codes (ICD-10 starter list)", "Catalogues")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::DiagnosisCodeDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/catalogue/diagnoses", diagnoses, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::CATALOGUE_READ);
      return createDtoResponse(Status::CODE_200, m_svc->searchDiagnoses(q(request, "q").value_or("")));
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
