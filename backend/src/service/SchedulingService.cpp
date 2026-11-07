#include "SchedulingService.hpp"

#include <regex>

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "service/AppointmentStateMachine.hpp"

namespace cf {

using text::opt;
using text::trimmed;

static const char* APPT_SELECT =
    "SELECT a.*, p.patient_number, p.full_name AS patient_name, p.mobile AS patient_mobile, p.gender AS patient_gender, "
    "  date_part('year', age(p.date_of_birth))::int AS patient_age, d.display_name AS doctor_name, "
    "  q.token_number, q.status AS token_status, c.id AS consultation_id, b.id AS bill_id, b.status AS bill_status "
    "FROM appointments a JOIN patients p ON p.id = a.patient_id JOIN doctors d ON d.id = a.doctor_id "
    "LEFT JOIN queue_tokens q ON q.appointment_id = a.id LEFT JOIN consultations c ON c.appointment_id = a.id "
    "LEFT JOIN LATERAL (SELECT id, status FROM bills WHERE appointment_id = a.id AND status <> 'CANCELLED' "
    "                   ORDER BY created_at DESC LIMIT 1) b ON true ";

static oatpp::Object<dto::AppointmentDto> apptFromRow(const Row& r) {
  auto a = dto::AppointmentDto::createShared();
  a->id = r.s("id");
  a->patientId = r.s("patient_id");
  a->patientNumber = r.s("patient_number");
  a->patientName = r.s("patient_name");
  a->patientMobile = r.s("patient_mobile");
  a->patientGender = r.s("patient_gender");
  a->patientAgeYears = r.i32("patient_age");
  a->doctorId = r.s("doctor_id");
  a->doctorName = r.s("doctor_name");
  a->appointmentType = r.s("appointment_type");
  a->startAt = r.ts("start_at");
  a->endAt = r.ts("end_at");
  a->status = r.s("status");
  a->reason = r.s("reason");
  a->notes = r.s("notes");
  a->followUpOf = r.s("follow_up_of");
  a->cancellationReason = r.s("cancellation_reason");
  a->checkedInAt = r.ts("checked_in_at");
  a->completedAt = r.ts("completed_at");
  a->tokenNumber = r.i32("token_number");
  a->tokenStatus = r.s("token_status");
  a->consultationId = r.s("consultation_id");
  a->billId = r.s("bill_id");
  a->billStatus = r.s("bill_status");
  a->version = r.i32("version");
  a->createdAt = r.ts("created_at");
  return a;
}

oatpp::Object<dto::AppointmentDto> SchedulingService::fetch(Conn& c, const std::string& id) {
  if (!text::isUuid(id)) throw ApiError::notFound("Appointment");
  auto r = c.exec(std::string(APPT_SELECT) + "WHERE a.id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Appointment");
  return apptFromRow(r.first());
}

void SchedulingService::transition(Conn& c, const AuthContext& ctx, const std::string& id, const std::string& from,
                                   const std::string& to, const std::string& note) {
  if (!canTransition(from, to))
    throw ApiError::conflict("INVALID_STATE", "Cannot change appointment from " + from + " to " + to);
  auto r = c.exec(
      "UPDATE appointments SET status = $3, version = version + 1, "
      "  checked_in_at = CASE WHEN $3 = 'CHECKED_IN' THEN now() ELSE checked_in_at END, "
      "  consultation_started_at = CASE WHEN $3 = 'IN_CONSULTATION' THEN now() ELSE consultation_started_at END, "
      "  completed_at = CASE WHEN $3 = 'COMPLETED' THEN now() ELSE completed_at END, "
      "  cancelled_at = CASE WHEN $3 = 'CANCELLED' THEN now() ELSE cancelled_at END "
      "WHERE id = $1 AND status = $2 RETURNING id",
      Params(id, from, to));
  if (r.empty()) throw ApiError::conflict("INVALID_STATE", "Appointment status changed; reload and try again");
  c.exec("INSERT INTO appointment_status_history (appointment_id, from_status, to_status, changed_by, note) "
         "VALUES ($1, $2, $3, $4, $5)",
         Params(id, from, to, ctx.userId, note.empty() ? std::optional<std::string>() : note));
}

// ---------------- Doctors ----------------
static oatpp::Object<dto::DoctorDto> doctorFromRow(const Row& r) {
  auto d = dto::DoctorDto::createShared();
  d->id = r.s("id");
  d->userId = r.s("user_id");
  d->displayName = r.s("display_name");
  d->qualification = r.s("qualification");
  d->specialization = r.s("specialization");
  d->registrationNumber = r.s("registration_number");
  d->registrationCouncil = r.s("registration_council");
  d->hprId = r.s("hpr_id");
  d->consultationFeePaise = r.i64("consultation_fee_paise");
  d->isActive = r.b("is_active");
  return d;
}

oatpp::Vector<oatpp::Object<dto::DoctorDto>> SchedulingService::listDoctors(bool includeInactive) const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::DoctorDto>>::createShared();
    c.exec(std::string("SELECT * FROM doctors ") + (includeInactive ? "" : "WHERE is_active ") + "ORDER BY display_name")
        .each([&](const Row& r) { out->push_back(doctorFromRow(r)); });
    return out;
  });
}

static void validateDoctor(const oatpp::Object<dto::DoctorInputDto>& in) {
  Validator v;
  v.check(trimmed(in->displayName).size() >= 3, "displayName", "Display name is required");
  if (in->consultationFeePaise) v.check(*in->consultationFeePaise >= 0 && *in->consultationFeePaise <= 10000000,
                                        "consultationFeePaise", "Fee must be between 0 and 1,00,000");
  v.throwIfAny();
}

oatpp::Object<dto::DoctorDto> SchedulingService::createDoctor(const AuthContext& ctx, const oatpp::Object<dto::DoctorInputDto>& in) {
  validateDoctor(in);
  std::string userId = trimmed(in->userId);
  if (!text::isUuid(userId)) throw ApiError::badRequest("A linked user account is required", {{"userId", "Select a user"}});
  return m_db->tx([&](Conn& c) {
    auto hasRole = c.exec("SELECT 1 FROM user_roles ur JOIN roles r ON r.id = ur.role_id WHERE ur.user_id = $1 AND r.code = 'DOCTOR'",
                          Params(userId));
    if (hasRole.empty()) throw ApiError::unprocessable("NOT_A_DOCTOR", "The linked user must have the DOCTOR role");
    try {
      auto r = c.exec("INSERT INTO doctors (user_id, display_name, qualification, specialization, registration_number, "
                      "registration_council, hpr_id, consultation_fee_paise) VALUES ($1,$2,$3,$4,$5,$6,$7,coalesce($8, 50000)) "
                      "RETURNING *",
                      Params(userId, trimmed(in->displayName), opt(in->qualification), opt(in->specialization),
                             opt(in->registrationNumber), opt(in->registrationCouncil), opt(in->hprId), in->consultationFeePaise));
      auto d = doctorFromRow(r.first());
      m_audit->record(c, ctx, AuditEvent{"DOCTOR_CREATE", "doctor", *d->id, std::nullopt, "SUCCESS", {{"userId", userId}}});
      return d;
    } catch (const DbError& e) {
      if (e.isUniqueViolation()) throw ApiError::conflict("DOCTOR_EXISTS", "This user already has a doctor profile");
      throw;
    }
  });
}

oatpp::Object<dto::DoctorDto> SchedulingService::updateDoctor(const AuthContext& ctx, const std::string& id,
                                                              const oatpp::Object<dto::DoctorInputDto>& in) {
  validateDoctor(in);
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("UPDATE doctors SET display_name = $2, qualification = $3, specialization = $4, registration_number = $5, "
                    "registration_council = $6, hpr_id = $7, consultation_fee_paise = coalesce($8, consultation_fee_paise), "
                    "is_active = coalesce($9, is_active) WHERE id = $1 RETURNING *",
                    Params(id, trimmed(in->displayName), opt(in->qualification), opt(in->specialization),
                           opt(in->registrationNumber), opt(in->registrationCouncil), opt(in->hprId),
                           in->consultationFeePaise, in->isActive));
    if (r.empty()) throw ApiError::notFound("Doctor");
    m_audit->record(c, ctx, AuditEvent{"DOCTOR_UPDATE", "doctor", id, std::nullopt, "SUCCESS", {}});
    return doctorFromRow(r.first());
  });
}

oatpp::Vector<oatpp::Object<dto::ScheduleBlockDto>> SchedulingService::getSchedule(const std::string& doctorId) const {
  return m_db->withConn([&](Conn& c) {
    if (!text::isUuid(doctorId) || c.exec("SELECT 1 FROM doctors WHERE id = $1", Params(doctorId)).empty())
      throw ApiError::notFound("Doctor");
    auto out = oatpp::Vector<oatpp::Object<dto::ScheduleBlockDto>>::createShared();
    c.exec("SELECT id, weekday, to_char(start_time, 'HH24:MI') AS st, to_char(end_time, 'HH24:MI') AS et, slot_minutes "
           "FROM doctor_schedules WHERE doctor_id = $1 AND is_active ORDER BY weekday, start_time",
           Params(doctorId))
        .each([&](const Row& r) {
          auto b = dto::ScheduleBlockDto::createShared();
          b->id = r.s("id");
          b->weekday = r.i32("weekday");
          b->startTime = r.s("st");
          b->endTime = r.s("et");
          b->slotMinutes = r.i32("slot_minutes");
          out->push_back(b);
        });
    return out;
  });
}

oatpp::Vector<oatpp::Object<dto::ScheduleBlockDto>> SchedulingService::replaceSchedule(
    const AuthContext& ctx, const std::string& doctorId, const oatpp::Object<dto::ScheduleUpdateDto>& in) {
  static const std::regex hhmm("^([01][0-9]|2[0-3]):[0-5][0-9]$");
  Validator v;
  if (!in->blocks) throw ApiError::badRequest("blocks is required");
  if (in->blocks->size() > 50) throw ApiError::badRequest("Too many schedule blocks");
  int i = 0;
  for (auto& b : *in->blocks) {
    std::string k = "blocks[" + std::to_string(i++) + "]";
    std::string st = trimmed(b->startTime), et = trimmed(b->endTime);
    v.check(b->weekday && *b->weekday >= 1 && *b->weekday <= 7, k + ".weekday", "Weekday 1 (Mon) to 7 (Sun)");
    v.check(std::regex_match(st, hhmm), k + ".startTime", "Use HH:MM");
    v.check(std::regex_match(et, hhmm), k + ".endTime", "Use HH:MM");
    v.check(st < et, k + ".endTime", "End must be after start");
    v.check(b->slotMinutes && *b->slotMinutes >= 5 && *b->slotMinutes <= 120, k + ".slotMinutes", "5 to 120 minutes");
  }
  v.throwIfAny();
  m_db->tx([&](Conn& c) {
    if (!text::isUuid(doctorId) || c.exec("SELECT 1 FROM doctors WHERE id = $1", Params(doctorId)).empty())
      throw ApiError::notFound("Doctor");
    c.exec("DELETE FROM doctor_schedules WHERE doctor_id = $1", Params(doctorId));
    try {
      for (auto& b : *in->blocks)
        c.exec("INSERT INTO doctor_schedules (doctor_id, weekday, start_time, end_time, slot_minutes) "
               "VALUES ($1, $2, $3::time, $4::time, $5)",
               Params(doctorId, b->weekday, trimmed(b->startTime), trimmed(b->endTime), b->slotMinutes));
    } catch (const DbError& e) {
      if (e.isExclusionViolation()) throw ApiError::conflict("OVERLAPPING_SCHEDULE", "Schedule blocks overlap on the same weekday");
      throw;
    }
    m_audit->record(c, ctx, AuditEvent{"SCHEDULE_UPDATE", "doctor", doctorId, std::nullopt, "SUCCESS",
                                       {{"blocks", (int)in->blocks->size()}}});
  });
  return getSchedule(doctorId);
}

oatpp::Vector<oatpp::Object<dto::SlotDto>> SchedulingService::slots(const std::string& doctorId, const std::string& date) const {
  if (!text::isIsoDate(date)) throw ApiError::badRequest("date must be YYYY-MM-DD");
  if (!text::isUuid(doctorId)) throw ApiError::notFound("Doctor");
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::SlotDto>>::createShared();
    c.exec(
         "WITH tz AS (SELECT timezone AS z FROM clinic_settings WHERE id = 1), "
         "sched AS (SELECT * FROM doctor_schedules s JOIN doctors d ON d.id = s.doctor_id "
         "          WHERE s.doctor_id = $1 AND s.is_active AND d.is_active AND s.weekday = extract(isodow FROM $2::date)), "
         "gen AS (SELECT gs AS local_start, gs + make_interval(mins => sched.slot_minutes) AS local_end FROM sched, "
         "        generate_series($2::date + sched.start_time, $2::date + sched.end_time - make_interval(mins => sched.slot_minutes), "
         "                        make_interval(mins => sched.slot_minutes)) gs), "
         "slots AS (SELECT local_start AT TIME ZONE (SELECT z FROM tz) AS s, local_end AT TIME ZONE (SELECT z FROM tz) AS e FROM gen) "
         "SELECT s AS start_at, e AS end_at, (s < now()) AS past, "
         "  EXISTS (SELECT 1 FROM appointments a WHERE a.doctor_id = $1 AND a.appointment_type <> 'WALK_IN' "
         "          AND a.status IN ('SCHEDULED','CHECKED_IN','IN_CONSULTATION','COMPLETED') "
         "          AND tstzrange(a.start_at, a.end_at, '[)') && tstzrange(slots.s, slots.e, '[)')) AS booked "
         "FROM slots ORDER BY s",
         Params(doctorId, date))
        .each([&](const Row& r) {
          auto s = dto::SlotDto::createShared();
          s->startAt = r.ts("start_at");
          s->endAt = r.ts("end_at");
          bool booked = r.boolv("booked"), past = r.boolv("past");
          s->available = !booked && !past;
          if (booked) s->reason = "BOOKED";
          else if (past) s->reason = "PAST";
          out->push_back(s);
        });
    return out;
  });
}

// ---------------- Appointments ----------------
oatpp::Vector<oatpp::Object<dto::AppointmentDto>> SchedulingService::list(const AppointmentFilter& f) const {
  std::string where = "WHERE 1=1";
  Params p;
  int n = 0;
  auto add = [&](const std::optional<std::string>& v, const std::string& clause) {
    if (!v) return;
    p.add(*v);
    std::string cl = clause;
    size_t pos;
    while ((pos = cl.find('?')) != std::string::npos) cl.replace(pos, 1, "$" + std::to_string(n + 1));
    ++n;
    where += " AND " + cl;
  };
  if (f.date && !text::isIsoDate(*f.date)) throw ApiError::badRequest("date must be YYYY-MM-DD");
  add(f.date,
      "a.start_at >= (?::date::timestamp AT TIME ZONE (SELECT timezone FROM clinic_settings WHERE id = 1)) "
      "AND a.start_at < ((?::date + 1)::timestamp AT TIME ZONE (SELECT timezone FROM clinic_settings WHERE id = 1))");
  add(f.from, "a.start_at >= ?::timestamptz");
  add(f.to, "a.start_at < ?::timestamptz");
  add(f.doctorId, "a.doctor_id = ?::uuid");
  add(f.patientId, "a.patient_id = ?::uuid");
  add(f.status, "a.status = ?");
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::AppointmentDto>>::createShared();
    std::string order = f.patientId ? " ORDER BY a.start_at DESC" : " ORDER BY a.start_at";
    c.exec(std::string(APPT_SELECT) + where + order + " LIMIT 500", p).each([&](const Row& r) { out->push_back(apptFromRow(r)); });
    return out;
  });
}

oatpp::Object<dto::AppointmentDto> SchedulingService::get(const std::string& id) const {
  return m_db->withConn([&](Conn& c) { return fetch(c, id); });
}

int SchedulingService::validateSlot(Conn& c, const std::string& doctorId, const std::string& startAt,
                                    const std::string& excludeId) const {
  if (!text::isUuid(doctorId)) throw ApiError::badRequest("Select a doctor", {{"doctorId", "Required"}});
  auto doc = c.exec("SELECT is_active FROM doctors WHERE id = $1", Params(doctorId));
  if (doc.empty()) throw ApiError::notFound("Doctor");
  if (!doc.first().boolv("is_active")) throw ApiError::unprocessable("DOCTOR_INACTIVE", "Doctor is not accepting appointments");
  auto r = c.exec(
      "WITH tz AS (SELECT timezone AS z FROM clinic_settings WHERE id = 1), "
      "st AS (SELECT ($2::timestamptz AT TIME ZONE (SELECT z FROM tz)) AS l) "
      "SELECT s.slot_minutes, ($2::timestamptz < now() - interval '5 minutes') AS in_past FROM doctor_schedules s, st "
      "WHERE s.doctor_id = $1 AND s.is_active AND s.weekday = extract(isodow FROM st.l) "
      "  AND st.l::time >= s.start_time AND st.l::time + make_interval(mins => s.slot_minutes) <= s.end_time "
      "  AND (extract(epoch FROM (st.l::time - s.start_time)) / 60)::int % s.slot_minutes = 0 "
      "  AND extract(second FROM st.l) = 0",
      Params(doctorId, startAt));
  if (r.empty())
    throw ApiError::unprocessable("SLOT_OUTSIDE_SCHEDULE", "The selected time is not a valid slot in the doctor's schedule");
  if (r.first().boolv("in_past")) throw ApiError::badRequest("Appointment time is in the past", {{"startAt", "Choose a future slot"}});
  return static_cast<int>(r.first().i64v("slot_minutes"));
}

oatpp::Object<dto::AppointmentDto> SchedulingService::book(const AuthContext& ctx, const oatpp::Object<dto::AppointmentCreateDto>& in) {
  Validator v;
  std::string patientId = trimmed(in->patientId), doctorId = trimmed(in->doctorId), startAt = trimmed(in->startAt);
  std::string type = text::upper(trimmed(in->appointmentType));
  if (type.empty()) type = "SCHEDULED";
  auto followUpOf = opt(in->followUpOf);
  v.check(text::isUuid(patientId), "patientId", "Select a patient");
  v.check(text::isUuid(doctorId), "doctorId", "Select a doctor");
  v.check(!startAt.empty(), "startAt", "Select a slot");
  v.check(type == "SCHEDULED" || type == "FOLLOW_UP", "appointmentType", "SCHEDULED or FOLLOW_UP");
  if (type == "FOLLOW_UP") v.check(followUpOf.has_value() && text::isUuid(*followUpOf), "followUpOf", "Follow-ups must reference the earlier visit");
  v.throwIfAny();

  return m_db->tx([&](Conn& c) {
    auto pt = c.exec("SELECT is_active FROM patients WHERE id = $1", Params(patientId));
    if (pt.empty()) throw ApiError::notFound("Patient");
    if (!pt.first().boolv("is_active")) throw ApiError::unprocessable("PATIENT_INACTIVE", "Patient record is inactive");
    if (followUpOf) {
      auto prev = c.exec("SELECT patient_id FROM appointments WHERE id = $1", Params(*followUpOf));
      if (prev.empty() || prev.first().str("patient_id") != patientId)
        throw ApiError::badRequest("Follow-up must reference an earlier visit of the same patient");
    }
    int slot = validateSlot(c, doctorId, startAt, "");
    auto clash = c.exec("SELECT 1 FROM appointments WHERE patient_id = $1 AND status IN ('SCHEDULED','CHECKED_IN') "
                        "AND tstzrange(start_at, end_at, '[)') && tstzrange($2::timestamptz, $2::timestamptz + make_interval(mins => $3), '[)')",
                        Params(patientId, startAt, slot));
    if (!clash.empty()) throw ApiError::conflict("PATIENT_ALREADY_BOOKED", "Patient already has an appointment at this time");
    std::string id;
    try {
      id = c.exec("INSERT INTO appointments (patient_id, doctor_id, appointment_type, start_at, end_at, reason, notes, follow_up_of, created_by) "
                  "VALUES ($1, $2, $3, $4::timestamptz, $4::timestamptz + make_interval(mins => $5), $6, $7, $8, $9) RETURNING id",
                  Params(patientId, doctorId, type, startAt, slot, opt(in->reason), opt(in->notes), followUpOf, ctx.userId))
               .first()
               .str("id");
    } catch (const DbError& e) {
      if (e.isExclusionViolation()) throw ApiError::conflict("SLOT_UNAVAILABLE", "This slot has just been booked. Please choose another.");
      throw;
    }
    c.exec("INSERT INTO appointment_status_history (appointment_id, from_status, to_status, changed_by, note) "
           "VALUES ($1, NULL, 'SCHEDULED', $2, $3)", Params(id, ctx.userId, type == "FOLLOW_UP" ? "Follow-up booked" : "Booked"));
    m_notify->queueAppointmentReminder(c, id);
    m_audit->record(c, ctx, AuditEvent{"APPOINTMENT_CREATE", "appointment", id, patientId, "SUCCESS",
                                       {{"doctorId", doctorId}, {"startAt", startAt}, {"type", type}}});
    return fetch(c, id);
  });
}

std::string SchedulingService::issueToken(Conn& c, const std::string& appointmentId) const {
  auto a = c.exec("SELECT a.doctor_id, (now() AT TIME ZONE s.timezone)::date::text AS today FROM appointments a, clinic_settings s "
                  "WHERE a.id = $1 AND s.id = 1", Params(appointmentId)).first();
  std::string doctorId = a.str("doctor_id"), today = a.str("today");
  long long n = c.exec("INSERT INTO id_counters (counter_name, period, last_value) VALUES ($1, $2, 1) "
                       "ON CONFLICT (counter_name, period) DO UPDATE SET last_value = id_counters.last_value + 1 RETURNING last_value",
                       Params("token:" + doctorId, today))
                    .first()
                    .i64v("last_value");
  return c.exec("INSERT INTO queue_tokens (appointment_id, doctor_id, queue_date, token_number) VALUES ($1, $2, $3::date, $4) RETURNING id",
                Params(appointmentId, doctorId, today, n))
      .first()
      .str("id");
}

oatpp::Object<dto::AppointmentDto> SchedulingService::walkIn(const AuthContext& ctx, const oatpp::Object<dto::WalkInDto>& in) {
  std::string patientId = trimmed(in->patientId), doctorId = trimmed(in->doctorId);
  Validator v;
  v.check(text::isUuid(patientId), "patientId", "Select a patient");
  v.check(text::isUuid(doctorId), "doctorId", "Select a doctor");
  v.throwIfAny();
  return m_db->tx([&](Conn& c) {
    auto pt = c.exec("SELECT is_active FROM patients WHERE id = $1", Params(patientId));
    if (pt.empty()) throw ApiError::notFound("Patient");
    auto doc = c.exec("SELECT is_active FROM doctors WHERE id = $1", Params(doctorId));
    if (doc.empty()) throw ApiError::notFound("Doctor");
    if (!doc.first().boolv("is_active")) throw ApiError::unprocessable("DOCTOR_INACTIVE", "Doctor is not accepting patients");
    auto open = c.exec("SELECT 1 FROM appointments WHERE patient_id = $1 AND doctor_id = $2 "
                       "AND status IN ('CHECKED_IN','IN_CONSULTATION')", Params(patientId, doctorId));
    if (!open.empty()) throw ApiError::conflict("ALREADY_IN_QUEUE", "Patient is already in this doctor's queue");
    auto id = c.exec("INSERT INTO appointments (patient_id, doctor_id, appointment_type, start_at, end_at, status, reason, "
                     "checked_in_at, created_by) SELECT $1, $2, 'WALK_IN', now(), now() + make_interval(mins => default_slot_minutes), "
                     "'CHECKED_IN', $3, now(), $4 FROM clinic_settings WHERE id = 1 RETURNING id",
                     Params(patientId, doctorId, opt(in->reason), ctx.userId))
                  .first()
                  .str("id");
    c.exec("INSERT INTO appointment_status_history (appointment_id, from_status, to_status, changed_by, note) "
           "VALUES ($1, NULL, 'CHECKED_IN', $2, 'Walk-in registered')", Params(id, ctx.userId));
    issueToken(c, id);
    m_audit->record(c, ctx, AuditEvent{"APPOINTMENT_WALK_IN", "appointment", id, patientId, "SUCCESS", {{"doctorId", doctorId}}});
    return fetch(c, id);
  });
}

oatpp::Object<dto::AppointmentDto> SchedulingService::reschedule(const AuthContext& ctx, const std::string& id,
                                                                 const oatpp::Object<dto::RescheduleDto>& in) {
  std::string startAt = trimmed(in->startAt);
  if (startAt.empty()) throw ApiError::badRequest("Select a new slot", {{"startAt", "Required"}});
  return m_db->tx([&](Conn& c) {
    auto cur = fetch(c, id);
    if (std::string(*cur->status) != "SCHEDULED")
      throw ApiError::conflict("INVALID_STATE", "Only scheduled (not yet checked-in) appointments can be rescheduled");
    if (in->version && *in->version != *cur->version)
      throw ApiError::conflict("STALE_VERSION", "Appointment was changed by someone else. Reload and try again.");
    std::string doctorId = opt(in->doctorId).value_or(std::string(*cur->doctorId));
    int slot = validateSlot(c, doctorId, startAt, id);
    try {
      c.exec("UPDATE appointments SET start_at = $2::timestamptz, end_at = $2::timestamptz + make_interval(mins => $3), "
             "doctor_id = $4, version = version + 1 WHERE id = $1",
             Params(id, startAt, slot, doctorId));
    } catch (const DbError& e) {
      if (e.isExclusionViolation()) throw ApiError::conflict("SLOT_UNAVAILABLE", "The new slot is not available");
      throw;
    }
    std::string note = "Rescheduled from " + std::string(*cur->startAt) + (opt(in->reason) ? ": " + *opt(in->reason) : "");
    c.exec("INSERT INTO appointment_status_history (appointment_id, from_status, to_status, changed_by, note) "
           "VALUES ($1, 'SCHEDULED', 'SCHEDULED', $2, $3)", Params(id, ctx.userId, note));
    m_notify->cancelForEntity(c, "appointment", id);
    m_notify->queueAppointmentReminder(c, id);
    m_audit->record(c, ctx, AuditEvent{"APPOINTMENT_RESCHEDULE", "appointment", id, std::string(*cur->patientId), "SUCCESS",
                                       {{"from", std::string(*cur->startAt)}, {"to", startAt}, {"doctorId", doctorId}}});
    return fetch(c, id);
  });
}

oatpp::Object<dto::AppointmentDto> SchedulingService::cancel(const AuthContext& ctx, const std::string& id, const std::string& reason) {
  std::string r = text::trim(reason);
  if (r.size() < 3) throw ApiError::badRequest("A cancellation reason is required", {{"reason", "Required"}});
  return m_db->tx([&](Conn& c) {
    auto cur = fetch(c, id);
    transition(c, ctx, id, *cur->status, "CANCELLED", r);
    c.exec("UPDATE appointments SET cancellation_reason = $2 WHERE id = $1", Params(id, r));
    c.exec("UPDATE queue_tokens SET status = 'CANCELLED' WHERE appointment_id = $1", Params(id));
    m_notify->cancelForEntity(c, "appointment", id);
    m_audit->record(c, ctx, AuditEvent{"APPOINTMENT_CANCEL", "appointment", id, std::string(*cur->patientId), "SUCCESS", {{"reason", r}}});
    return fetch(c, id);
  });
}

oatpp::Object<dto::AppointmentDto> SchedulingService::checkIn(const AuthContext& ctx, const std::string& id) {
  return m_db->tx([&](Conn& c) {
    auto cur = fetch(c, id);
    auto sameDay = c.exec("SELECT ((a.start_at AT TIME ZONE s.timezone)::date = (now() AT TIME ZONE s.timezone)::date) AS ok "
                          "FROM appointments a, clinic_settings s WHERE a.id = $1 AND s.id = 1", Params(id));
    if (!sameDay.first().boolv("ok")) throw ApiError::unprocessable("NOT_TODAY", "Only today's appointments can be checked in");
    transition(c, ctx, id, *cur->status, "CHECKED_IN", "");
    issueToken(c, id);
    m_audit->record(c, ctx, AuditEvent{"APPOINTMENT_CHECK_IN", "appointment", id, std::string(*cur->patientId), "SUCCESS", {}});
    return fetch(c, id);
  });
}

oatpp::Object<dto::AppointmentDto> SchedulingService::markNoShow(const AuthContext& ctx, const std::string& id) {
  return m_db->tx([&](Conn& c) {
    auto cur = fetch(c, id);
    auto started = c.exec("SELECT start_at < now() AS ok FROM appointments WHERE id = $1", Params(id)).first().boolv("ok");
    if (!started) throw ApiError::unprocessable("TOO_EARLY", "An appointment can be marked no-show only after its start time");
    transition(c, ctx, id, *cur->status, "NO_SHOW", "");
    m_notify->cancelForEntity(c, "appointment", id);
    m_audit->record(c, ctx, AuditEvent{"APPOINTMENT_NO_SHOW", "appointment", id, std::string(*cur->patientId), "SUCCESS", {}});
    return fetch(c, id);
  });
}

oatpp::Vector<oatpp::Object<dto::AppointmentHistoryDto>> SchedulingService::history(const std::string& id) const {
  return m_db->withConn([&](Conn& c) {
    fetch(c, id);
    auto out = oatpp::Vector<oatpp::Object<dto::AppointmentHistoryDto>>::createShared();
    c.exec("SELECT h.*, u.full_name AS by_name FROM appointment_status_history h LEFT JOIN users u ON u.id = h.changed_by "
           "WHERE h.appointment_id = $1 ORDER BY h.changed_at, h.id", Params(id))
        .each([&](const Row& r) {
          auto h = dto::AppointmentHistoryDto::createShared();
          h->fromStatus = r.s("from_status");
          h->toStatus = r.s("to_status");
          h->changedBy = r.s("by_name");
          h->changedAt = r.ts("changed_at");
          h->note = r.s("note");
          out->push_back(h);
        });
    return out;
  });
}

// ---------------- Queue ----------------
static const char* TOKEN_SELECT =
    "SELECT q.*, q.queue_date::text AS qdate, d.display_name AS doctor_name, p.id AS patient_id, p.full_name AS patient_name, "
    "  p.patient_number, a.appointment_type, a.status AS appointment_status, a.start_at, c.id AS consultation_id, "
    "  CASE WHEN q.status IN ('WAITING','CALLED') THEN (extract(epoch FROM now() - q.issued_at) / 60)::int END AS waiting_minutes "
    "FROM queue_tokens q JOIN appointments a ON a.id = q.appointment_id JOIN patients p ON p.id = a.patient_id "
    "JOIN doctors d ON d.id = q.doctor_id LEFT JOIN consultations c ON c.appointment_id = a.id ";

static oatpp::Object<dto::QueueTokenDto> tokenFromRow(const Row& r) {
  auto t = dto::QueueTokenDto::createShared();
  t->id = r.s("id");
  t->appointmentId = r.s("appointment_id");
  t->doctorId = r.s("doctor_id");
  t->doctorName = r.s("doctor_name");
  t->queueDate = r.s("qdate");
  t->tokenNumber = r.i32("token_number");
  char buf[16];
  snprintf(buf, sizeof(buf), "T-%03d", (int)*t->tokenNumber);
  t->displayToken = buf;
  t->status = r.s("status");
  t->patientId = r.s("patient_id");
  t->patientName = r.s("patient_name");
  t->patientNumber = r.s("patient_number");
  t->appointmentType = r.s("appointment_type");
  t->appointmentStatus = r.s("appointment_status");
  t->scheduledAt = r.ts("start_at");
  t->issuedAt = r.ts("issued_at");
  t->calledAt = r.ts("called_at");
  t->waitingMinutes = r.i32("waiting_minutes");
  t->consultationId = r.s("consultation_id");
  return t;
}

oatpp::Object<dto::QueueTokenDto> SchedulingService::fetchToken(Conn& c, const std::string& tokenId) const {
  if (!text::isUuid(tokenId)) throw ApiError::notFound("Queue token");
  auto r = c.exec(std::string(TOKEN_SELECT) + "WHERE q.id = $1", Params(tokenId));
  if (r.empty()) throw ApiError::notFound("Queue token");
  return tokenFromRow(r.first());
}

oatpp::Vector<oatpp::Object<dto::QueueTokenDto>> SchedulingService::queue(const std::optional<std::string>& date,
                                                                          const std::optional<std::string>& doctorId) const {
  if (date && !text::isIsoDate(*date)) throw ApiError::badRequest("date must be YYYY-MM-DD");
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::QueueTokenDto>>::createShared();
    c.exec(std::string(TOKEN_SELECT) +
               "WHERE q.queue_date = coalesce($1::date, (SELECT (now() AT TIME ZONE timezone)::date FROM clinic_settings WHERE id = 1)) "
               "AND ($2::uuid IS NULL OR q.doctor_id = $2::uuid) "
               "ORDER BY array_position(ARRAY['IN_CONSULTATION','CALLED','WAITING','SKIPPED','DONE','CANCELLED'], q.status), "
               "q.doctor_id, q.token_number",
           Params(date, doctorId))
        .each([&](const Row& r) { out->push_back(tokenFromRow(r)); });
    return out;
  });
}

oatpp::Object<dto::QueueTokenDto> SchedulingService::callToken(const AuthContext& ctx, const std::string& tokenId) {
  return m_db->tx([&](Conn& c) {
    auto t = fetchToken(c, tokenId);
    auto r = c.exec("UPDATE queue_tokens SET status = 'CALLED', called_at = now() WHERE id = $1 AND status IN ('WAITING','SKIPPED') RETURNING id",
                    Params(tokenId));
    if (r.empty()) throw ApiError::conflict("INVALID_STATE", "Only waiting or skipped tokens can be called");
    m_audit->record(c, ctx, AuditEvent{"QUEUE_CALL", "queue_token", tokenId, std::string(*t->patientId), "SUCCESS",
                                       {{"token", std::string(*t->displayToken)}}});
    return fetchToken(c, tokenId);
  });
}

oatpp::Object<dto::QueueTokenDto> SchedulingService::skipToken(const AuthContext& ctx, const std::string& tokenId) {
  return m_db->tx([&](Conn& c) {
    auto t = fetchToken(c, tokenId);
    auto r = c.exec("UPDATE queue_tokens SET status = 'SKIPPED' WHERE id = $1 AND status IN ('WAITING','CALLED') RETURNING id",
                    Params(tokenId));
    if (r.empty()) throw ApiError::conflict("INVALID_STATE", "Only waiting or called tokens can be skipped");
    m_audit->record(c, ctx, AuditEvent{"QUEUE_SKIP", "queue_token", tokenId, std::string(*t->patientId), "SUCCESS",
                                       {{"token", std::string(*t->displayToken)}}});
    return fetchToken(c, tokenId);
  });
}

}  // namespace cf
