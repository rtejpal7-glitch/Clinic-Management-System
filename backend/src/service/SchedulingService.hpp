#pragma once
// Doctors, working hours, slot availability, appointments and the waiting queue
// (NABH CMS AAC.2 scope).

#include <memory>
#include <optional>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "dto/SchedulingDtos.hpp"
#include "security/AuthContext.hpp"
#include "service/NotificationService.hpp"

namespace cf {

struct AppointmentFilter {
  std::optional<std::string> date, from, to, doctorId, patientId, status;
};

class SchedulingService {
 public:
  SchedulingService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit,
                    std::shared_ptr<NotificationService> notifications)
      : m_db(std::move(db)), m_audit(std::move(audit)), m_notify(std::move(notifications)) {}

  // Doctors & schedules
  oatpp::Vector<oatpp::Object<dto::DoctorDto>> listDoctors(bool includeInactive) const;
  oatpp::Object<dto::DoctorDto> createDoctor(const AuthContext& ctx, const oatpp::Object<dto::DoctorInputDto>& in);
  oatpp::Object<dto::DoctorDto> updateDoctor(const AuthContext& ctx, const std::string& id,
                                             const oatpp::Object<dto::DoctorInputDto>& in);
  oatpp::Vector<oatpp::Object<dto::ScheduleBlockDto>> getSchedule(const std::string& doctorId) const;
  oatpp::Vector<oatpp::Object<dto::ScheduleBlockDto>> replaceSchedule(const AuthContext& ctx, const std::string& doctorId,
                                                                      const oatpp::Object<dto::ScheduleUpdateDto>& in);
  oatpp::Vector<oatpp::Object<dto::SlotDto>> slots(const std::string& doctorId, const std::string& date) const;

  // Appointments
  oatpp::Vector<oatpp::Object<dto::AppointmentDto>> list(const AppointmentFilter& f) const;
  oatpp::Object<dto::AppointmentDto> get(const std::string& id) const;
  oatpp::Object<dto::AppointmentDto> book(const AuthContext& ctx, const oatpp::Object<dto::AppointmentCreateDto>& in);
  oatpp::Object<dto::AppointmentDto> walkIn(const AuthContext& ctx, const oatpp::Object<dto::WalkInDto>& in);
  oatpp::Object<dto::AppointmentDto> reschedule(const AuthContext& ctx, const std::string& id,
                                                const oatpp::Object<dto::RescheduleDto>& in);
  oatpp::Object<dto::AppointmentDto> cancel(const AuthContext& ctx, const std::string& id, const std::string& reason);
  oatpp::Object<dto::AppointmentDto> checkIn(const AuthContext& ctx, const std::string& id);
  oatpp::Object<dto::AppointmentDto> markNoShow(const AuthContext& ctx, const std::string& id);
  oatpp::Vector<oatpp::Object<dto::AppointmentHistoryDto>> history(const std::string& id) const;

  // Queue
  oatpp::Vector<oatpp::Object<dto::QueueTokenDto>> queue(const std::optional<std::string>& date,
                                                         const std::optional<std::string>& doctorId) const;
  oatpp::Object<dto::QueueTokenDto> callToken(const AuthContext& ctx, const std::string& tokenId);
  oatpp::Object<dto::QueueTokenDto> skipToken(const AuthContext& ctx, const std::string& tokenId);

  // Shared helpers (also used by ClinicalService)
  static void transition(Conn& c, const AuthContext& ctx, const std::string& appointmentId, const std::string& from,
                         const std::string& to, const std::string& note);
  static oatpp::Object<dto::AppointmentDto> fetch(Conn& c, const std::string& id);

 private:
  std::string issueToken(Conn& c, const std::string& appointmentId) const;
  int validateSlot(Conn& c, const std::string& doctorId, const std::string& startAt, const std::string& excludeId) const;
  oatpp::Object<dto::QueueTokenDto> fetchToken(Conn& c, const std::string& tokenId) const;

  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
  std::shared_ptr<NotificationService> m_notify;
};

}  // namespace cf
