#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

class DoctorDto : public oatpp::DTO {
  DTO_INIT(DoctorDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, userId);
  DTO_FIELD(String, displayName);
  DTO_FIELD(String, qualification);
  DTO_FIELD(String, specialization);
  DTO_FIELD(String, registrationNumber);
  DTO_FIELD(String, registrationCouncil);
  DTO_FIELD(String, hprId);
  DTO_FIELD(Int64, consultationFeePaise);
  DTO_FIELD(Boolean, isActive);
};

class DoctorInputDto : public oatpp::DTO {
  DTO_INIT(DoctorInputDto, DTO)
  DTO_FIELD(String, userId);
  DTO_FIELD(String, displayName);
  DTO_FIELD(String, qualification);
  DTO_FIELD(String, specialization);
  DTO_FIELD(String, registrationNumber);
  DTO_FIELD(String, registrationCouncil);
  DTO_FIELD(String, hprId);
  DTO_FIELD(Int64, consultationFeePaise);
  DTO_FIELD(Boolean, isActive);
};

class ScheduleBlockDto : public oatpp::DTO {
  DTO_INIT(ScheduleBlockDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(Int32, weekday);        // ISO: 1 = Monday ... 7 = Sunday
  DTO_FIELD(String, startTime);     // HH:MM
  DTO_FIELD(String, endTime);       // HH:MM
  DTO_FIELD(Int32, slotMinutes);
};

class ScheduleUpdateDto : public oatpp::DTO {
  DTO_INIT(ScheduleUpdateDto, DTO)
  DTO_FIELD(Vector<Object<ScheduleBlockDto>>, blocks);
};

class SlotDto : public oatpp::DTO {
  DTO_INIT(SlotDto, DTO)
  DTO_FIELD(String, startAt);
  DTO_FIELD(String, endAt);
  DTO_FIELD(Boolean, available);
  DTO_FIELD(String, reason);   // BOOKED | PAST
};

class AppointmentDto : public oatpp::DTO {
  DTO_INIT(AppointmentDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, patientNumber);
  DTO_FIELD(String, patientName);
  DTO_FIELD(String, patientMobile);
  DTO_FIELD(String, patientGender);
  DTO_FIELD(Int32, patientAgeYears);
  DTO_FIELD(String, doctorId);
  DTO_FIELD(String, doctorName);
  DTO_FIELD(String, appointmentType);
  DTO_FIELD(String, startAt);
  DTO_FIELD(String, endAt);
  DTO_FIELD(String, status);
  DTO_FIELD(String, reason);
  DTO_FIELD(String, notes);
  DTO_FIELD(String, followUpOf);
  DTO_FIELD(String, cancellationReason);
  DTO_FIELD(String, checkedInAt);
  DTO_FIELD(String, completedAt);
  DTO_FIELD(Int32, tokenNumber);
  DTO_FIELD(String, tokenStatus);
  DTO_FIELD(String, consultationId);
  DTO_FIELD(String, billId);
  DTO_FIELD(String, billStatus);
  DTO_FIELD(Int32, version);
  DTO_FIELD(String, createdAt);
};

class AppointmentCreateDto : public oatpp::DTO {
  DTO_INIT(AppointmentCreateDto, DTO)
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, doctorId);
  DTO_FIELD(String, startAt);            // ISO-8601 with offset, must match a free slot
  DTO_FIELD(String, appointmentType);    // SCHEDULED | FOLLOW_UP
  DTO_FIELD(String, followUpOf);
  DTO_FIELD(String, reason);
  DTO_FIELD(String, notes);
};

class WalkInDto : public oatpp::DTO {
  DTO_INIT(WalkInDto, DTO)
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, doctorId);
  DTO_FIELD(String, reason);
};

class RescheduleDto : public oatpp::DTO {
  DTO_INIT(RescheduleDto, DTO)
  DTO_FIELD(String, startAt);
  DTO_FIELD(String, doctorId);   // optional: move to another doctor
  DTO_FIELD(String, reason);
  DTO_FIELD(Int32, version);
};

class AppointmentHistoryDto : public oatpp::DTO {
  DTO_INIT(AppointmentHistoryDto, DTO)
  DTO_FIELD(String, fromStatus);
  DTO_FIELD(String, toStatus);
  DTO_FIELD(String, changedBy);
  DTO_FIELD(String, changedAt);
  DTO_FIELD(String, note);
};

class QueueTokenDto : public oatpp::DTO {
  DTO_INIT(QueueTokenDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, appointmentId);
  DTO_FIELD(String, doctorId);
  DTO_FIELD(String, doctorName);
  DTO_FIELD(String, queueDate);
  DTO_FIELD(Int32, tokenNumber);
  DTO_FIELD(String, displayToken);
  DTO_FIELD(String, status);
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, patientName);
  DTO_FIELD(String, patientNumber);
  DTO_FIELD(String, appointmentType);
  DTO_FIELD(String, appointmentStatus);
  DTO_FIELD(String, scheduledAt);
  DTO_FIELD(String, issuedAt);
  DTO_FIELD(String, calledAt);
  DTO_FIELD(Int32, waitingMinutes);
  DTO_FIELD(String, consultationId);
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
