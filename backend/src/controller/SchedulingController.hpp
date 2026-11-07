#pragma once
#include "BaseController.hpp"
#include "dto/SchedulingDtos.hpp"
#include "service/SchedulingService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class SchedulingController : public BaseController {
  std::shared_ptr<SchedulingService> m_svc;

  std::shared_ptr<OutgoingResponse> ok(const oatpp::Void& dto) { return createDtoResponse(Status::CODE_200, dto); }

 public:
  SchedulingController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth, bool tp,
                       std::shared_ptr<SchedulingService> svc)
      : BaseController(om, std::move(auth), tp), m_svc(std::move(svc)) {}

  // ---------- Doctors & schedules ----------
  ENDPOINT_INFO(listDoctors) {
    CF_DOC("List doctors (includeInactive=true for all)", "Doctors & Schedules")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::DoctorDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/doctors", listDoctors, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::SCHEDULE_READ);
      return createDtoResponse(Status::CODE_200, m_svc->listDoctors(q(request, "includeInactive").value_or("") == "true"));
    });
  }

  ENDPOINT_INFO(createDoctor) {
    CF_DOC("Create a doctor profile linked to a user with the DOCTOR role", "Doctors & Schedules")
    info->addConsumes<Object<dto::DoctorInputDto>>("application/json");
    info->addResponse<Object<dto::DoctorDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/doctors", createDoctor, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::DOCTOR_MANAGE);
      return createDtoResponse(Status::CODE_201, m_svc->createDoctor(ctx, parse<Object<dto::DoctorInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(updateDoctor) {
    CF_DOC("Update a doctor profile", "Doctors & Schedules")
    info->addConsumes<Object<dto::DoctorInputDto>>("application/json");
    info->addResponse<Object<dto::DoctorDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/doctors/{id}", updateDoctor, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::DOCTOR_MANAGE);
      return ok(m_svc->updateDoctor(ctx, id, parse<Object<dto::DoctorInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(getSchedule) {
    CF_DOC("Weekly working hours of a doctor", "Doctors & Schedules")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::ScheduleBlockDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/doctors/{id}/schedule", getSchedule, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::SCHEDULE_READ);
      return ok(m_svc->getSchedule(id));
    });
  }

  ENDPOINT_INFO(putSchedule) {
    CF_DOC("Replace a doctor's weekly working hours and slot lengths", "Doctors & Schedules")
    info->addConsumes<Object<dto::ScheduleUpdateDto>>("application/json");
    info->addResponse<oatpp::Vector<oatpp::Object<dto::ScheduleBlockDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/doctors/{id}/schedule", putSchedule, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::SCHEDULE_MANAGE);
      return ok(m_svc->replaceSchedule(ctx, id, parse<Object<dto::ScheduleUpdateDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(slots) {
    CF_DOC("Slot availability for a doctor on a date (date=YYYY-MM-DD, clinic timezone)", "Doctors & Schedules")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::SlotDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/doctors/{id}/slots", slots, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::SCHEDULE_READ);
      auto d = q(request, "date");
      if (!d) throw ApiError::badRequest("date is required");
      return ok(m_svc->slots(id, *d));
    });
  }

  // ---------- Appointments ----------
  ENDPOINT_INFO(list) {
    CF_DOC("List appointments (filters: date, from, to, doctorId, patientId, status)", "Appointments")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::AppointmentDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/appointments", list, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::APPOINTMENT_READ);
      AppointmentFilter f;
      f.date = q(request, "date");
      f.from = q(request, "from");
      f.to = q(request, "to");
      f.doctorId = q(request, "doctorId");
      f.patientId = q(request, "patientId");
      f.status = q(request, "status");
      return ok(m_svc->list(f));
    });
  }

  ENDPOINT_INFO(book) {
    CF_DOC("Book an appointment in a free slot (double-booking is rejected with 409)", "Appointments")
    info->addConsumes<Object<dto::AppointmentCreateDto>>("application/json");
    info->addResponse<Object<dto::AppointmentDto>>(Status::CODE_201, "application/json");
    info->addResponse<Object<dto::ErrorDto>>(Status::CODE_409, "application/json");
  }
  ENDPOINT("POST", "/api/v1/appointments", book, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::APPOINTMENT_MANAGE);
      return createDtoResponse(Status::CODE_201, m_svc->book(ctx, parse<Object<dto::AppointmentCreateDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(walkIn) {
    CF_DOC("Register a walk-in: creates a checked-in visit and issues a queue token", "Appointments")
    info->addConsumes<Object<dto::WalkInDto>>("application/json");
    info->addResponse<Object<dto::AppointmentDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/appointments/walk-in", walkIn, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::APPOINTMENT_MANAGE);
      return createDtoResponse(Status::CODE_201, m_svc->walkIn(ctx, parse<Object<dto::WalkInDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(get) {
    CF_DOC("Get an appointment", "Appointments")
    info->addResponse<Object<dto::AppointmentDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/appointments/{id}", get, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::APPOINTMENT_READ);
      return ok(m_svc->get(id));
    });
  }

  ENDPOINT_INFO(history) {
    CF_DOC("Status history of an appointment", "Appointments")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::AppointmentHistoryDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/appointments/{id}/history", history, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::APPOINTMENT_READ);
      return ok(m_svc->history(id));
    });
  }

  ENDPOINT_INFO(reschedule) {
    CF_DOC("Move a scheduled appointment to another free slot", "Appointments")
    info->addConsumes<Object<dto::RescheduleDto>>("application/json");
    info->addResponse<Object<dto::AppointmentDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/appointments/{id}/reschedule", reschedule, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::APPOINTMENT_MANAGE);
      return ok(m_svc->reschedule(ctx, id, parse<Object<dto::RescheduleDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(cancel) {
    CF_DOC("Cancel an appointment (reason required)", "Appointments")
    info->addConsumes<Object<dto::ReasonDto>>("application/json");
    info->addResponse<Object<dto::AppointmentDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/appointments/{id}/cancel", cancel, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::APPOINTMENT_MANAGE);
      auto in = parse<Object<dto::ReasonDto>>(bodyOf(request));
      return ok(m_svc->cancel(ctx, id, in->reason ? *in->reason : ""));
    });
  }

  ENDPOINT_INFO(checkIn) {
    CF_DOC("Check a patient in for today's appointment and issue a queue token", "Appointments")
    info->addResponse<Object<dto::AppointmentDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/appointments/{id}/check-in", checkIn, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_svc->checkIn(auth(request, perm::APPOINTMENT_MANAGE), id)); });
  }

  ENDPOINT_INFO(noShow) {
    CF_DOC("Mark an appointment as no-show (after its start time)", "Appointments")
    info->addResponse<Object<dto::AppointmentDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/appointments/{id}/no-show", noShow, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_svc->markNoShow(auth(request, perm::APPOINTMENT_MANAGE), id)); });
  }

  // ---------- Queue ----------
  ENDPOINT_INFO(queue) {
    CF_DOC("Waiting queue for a date (default today) and optional doctor", "Queue")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::QueueTokenDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/queue", queue, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::APPOINTMENT_READ);
      return ok(m_svc->queue(q(request, "date"), q(request, "doctorId")));
    });
  }

  ENDPOINT_INFO(call) {
    CF_DOC("Call a token to the consultation room", "Queue")
    info->addResponse<Object<dto::QueueTokenDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/queue/{id}/call", call, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_svc->callToken(auth(request, perm::QUEUE_MANAGE), id)); });
  }

  ENDPOINT_INFO(skip) {
    CF_DOC("Skip a token (patient not present)", "Queue")
    info->addResponse<Object<dto::QueueTokenDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/queue/{id}/skip", skip, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_svc->skipToken(auth(request, perm::QUEUE_MANAGE), id)); });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
