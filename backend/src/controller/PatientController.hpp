#pragma once
#include "BaseController.hpp"
#include "dto/BillingDtos.hpp"
#include "dto/ClinicalDtos.hpp"
#include "dto/PatientDtos.hpp"
#include "dto/SchedulingDtos.hpp"
#include "service/BillingService.hpp"
#include "service/ClinicalService.hpp"
#include "service/PatientService.hpp"
#include "service/SchedulingService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class PatientController : public BaseController {
  std::shared_ptr<PatientService> m_patients;
  std::shared_ptr<ClinicalService> m_clinical;
  std::shared_ptr<SchedulingService> m_scheduling;
  std::shared_ptr<BillingService> m_billing;

 public:
  PatientController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth, bool tp,
                    std::shared_ptr<PatientService> patients, std::shared_ptr<ClinicalService> clinical,
                    std::shared_ptr<SchedulingService> scheduling, std::shared_ptr<BillingService> billing)
      : BaseController(om, std::move(auth), tp), m_patients(std::move(patients)), m_clinical(std::move(clinical)),
        m_scheduling(std::move(scheduling)), m_billing(std::move(billing)) {}

  ENDPOINT_INFO(search) {
    CF_DOC("Search patients by name, mobile, patient number or ABHA number/address (q, page, size)", "Patients")
    info->queryParams.add<String>("q").required = false;
    info->addResponse<Object<dto::PatientPageDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients", search, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PATIENT_READ);
      return createDtoResponse(Status::CODE_200, m_patients->search(ctx, q(request, "q").value_or(""), qInt(request, "page", 1),
                                                                    qInt(request, "size", 20)));
    });
  }

  ENDPOINT_INFO(duplicateCheck) {
    CF_DOC("Check for possible duplicate patients before registration", "Patients")
    info->addConsumes<Object<dto::PatientInputDto>>("application/json");
    info->addResponse<Object<dto::DuplicateCheckResultDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/patients/duplicate-check", duplicateCheck, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::PATIENT_CREATE);
      return createDtoResponse(Status::CODE_200, m_patients->duplicateCheck(parse<Object<dto::PatientInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(create) {
    CF_DOC("Register a patient. Returns 409 POSSIBLE_DUPLICATE with candidates unless confirmNotDuplicate + reason are given", "Patients")
    info->addConsumes<Object<dto::PatientInputDto>>("application/json");
    info->addResponse<Object<dto::PatientDto>>(Status::CODE_201, "application/json");
    info->addResponse<Object<dto::ErrorDto>>(Status::CODE_409, "application/json");
  }
  ENDPOINT("POST", "/api/v1/patients", create, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PATIENT_CREATE);
      return createDtoResponse(Status::CODE_201, m_patients->create(ctx, parse<Object<dto::PatientInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(get) {
    CF_DOC("Patient demographics (access is audit-logged)", "Patients")
    info->addResponse<Object<dto::PatientDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}", get, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_patients->get(auth(request, perm::PATIENT_READ), id)); });
  }

  ENDPOINT_INFO(update) {
    CF_DOC("Update demographics (optimistic locking via version; field changes recorded in history)", "Patients")
    info->addConsumes<Object<dto::PatientInputDto>>("application/json");
    info->addResponse<Object<dto::PatientDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/patients/{id}", update, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PATIENT_UPDATE);
      return createDtoResponse(Status::CODE_200, m_patients->update(ctx, id, parse<Object<dto::PatientInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(history) {
    CF_DOC("Demographic change history", "Patients")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::PatientChangeDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}/history", history, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_patients->history(auth(request, perm::PATIENT_HISTORY_READ), id)); });
  }

  ENDPOINT_INFO(timeline) {
    CF_DOC("Clinical history: consultations, diagnoses, medicines, orders, vitals (newest first)", "Patients")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::TimelineEntryDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}/timeline", timeline, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_clinical->timeline(auth(request, perm::CLINICAL_READ), id)); });
  }

  ENDPOINT_INFO(appointments) {
    CF_DOC("Appointment history for a patient", "Patients")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::AppointmentDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}/appointments", appointments, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::APPOINTMENT_READ);
      AppointmentFilter f;
      f.patientId = std::string(*id);
      return createDtoResponse(Status::CODE_200, m_scheduling->list(f));
    });
  }

  ENDPOINT_INFO(bills) {
    CF_DOC("Billing history for a patient", "Patients")
    info->addResponse<Object<dto::BillPageDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}/bills", bills, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::BILLING_READ);
      BillFilter f;
      f.patientId = std::string(*id);
      f.size = 100;
      return createDtoResponse(Status::CODE_200, m_billing->list(f));
    });
  }

  ENDPOINT_INFO(allergies) {
    CF_DOC("Patient allergies", "Patients")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::AllergyDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/patients/{id}/allergies", allergies, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::CLINICAL_READ);
      return createDtoResponse(Status::CODE_200, m_patients->allergies(id));
    });
  }

  ENDPOINT_INFO(addAllergy) {
    CF_DOC("Record an allergy", "Patients")
    info->addConsumes<Object<dto::AllergyInputDto>>("application/json");
    info->addResponse<Object<dto::AllergyDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/patients/{id}/allergies", addAllergy, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return createDtoResponse(Status::CODE_201, m_patients->addAllergy(ctx, id, parse<Object<dto::AllergyInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(updateAllergy) {
    CF_DOC("Change allergy status (ACTIVE / INACTIVE / ENTERED_IN_ERROR)", "Patients")
    info->addConsumes<Object<dto::AllergyInputDto>>("application/json");
    info->addResponse<Object<dto::AllergyDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/patients/{id}/allergies/{allergyId}", updateAllergy, PATH(String, id), PATH(String, allergyId),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      auto in = parse<Object<dto::AllergyInputDto>>(bodyOf(request));
      return createDtoResponse(Status::CODE_200, m_patients->updateAllergyStatus(ctx, id, allergyId, in->status ? *in->status : ""));
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
