#include "ClinicalService.hpp"

#include <cmath>

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "service/SchedulingService.hpp"

namespace cf {

using text::opt;
using text::trimmed;

static oatpp::Object<dto::VitalsDto> vitalsFromRow(const Row& r) {
  auto v = dto::VitalsDto::createShared();
  v->bpSystolic = r.i32("bp_systolic");
  v->bpDiastolic = r.i32("bp_diastolic");
  v->pulse = r.i32("pulse");
  v->temperatureC = r.f64("temperature_c");
  v->respiratoryRate = r.i32("respiratory_rate");
  v->spo2 = r.i32("spo2");
  v->heightCm = r.f64("height_cm");
  v->weightKg = r.f64("weight_kg");
  v->recordedAt = r.ts("recorded_at");
  if (v->heightCm && v->weightKg && *v->heightCm > 0) {
    double m = *v->heightCm / 100.0;
    v->bmi = std::round(*v->weightKg / (m * m) * 10.0) / 10.0;
  }
  return v;
}

oatpp::Object<dto::ConsultationDto> ClinicalService::load(Conn& c, const std::string& id, const AuthContext* ctx) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Consultation");
  auto rows = c.exec(
      "SELECT c.*, c.follow_up_date::text AS fud, p.full_name AS patient_name, p.patient_number, d.display_name AS doctor_name, "
      "  d.qualification AS doctor_qualification, d.registration_number AS doctor_registration "
      "FROM consultations c JOIN patients p ON p.id = c.patient_id JOIN doctors d ON d.id = c.doctor_id WHERE c.id = $1",
      Params(id));
  if (rows.empty()) throw ApiError::notFound("Consultation");
  auto r = rows.first();
  auto d = dto::ConsultationDto::createShared();
  d->id = r.s("id");
  d->appointmentId = r.s("appointment_id");
  d->patientId = r.s("patient_id");
  d->patientName = r.s("patient_name");
  d->patientNumber = r.s("patient_number");
  d->doctorId = r.s("doctor_id");
  d->doctorName = r.s("doctor_name");
  d->doctorQualification = r.s("doctor_qualification");
  d->doctorRegistration = r.s("doctor_registration");
  d->status = r.s("status");
  d->chiefComplaint = r.s("chief_complaint");
  d->historyPresentIllness = r.s("history_present_illness");
  d->pastMedicalHistory = r.s("past_medical_history");
  d->currentMedications = r.s("current_medications");
  d->examinationNotes = r.s("examination_notes");
  d->treatmentPlan = r.s("treatment_plan");
  d->followUpInstructions = r.s("follow_up_instructions");
  d->followUpDate = r.s("fud");
  d->startedAt = r.ts("started_at");
  d->finalizedAt = r.ts("finalized_at");
  d->version = r.i32("version");
  d->specialtyCode = r.s("specialty_code");
  d->canEdit = ctx && ctx->doctorId && *ctx->doctorId == r.str("doctor_id") && r.str("status") == "DRAFT" &&
               ctx->has(perm::CLINICAL_WRITE);

  auto vit = c.exec("SELECT * FROM vital_signs WHERE consultation_id = $1", Params(id));
  if (!vit.empty()) d->vitals = vitalsFromRow(vit.first());

  d->diagnoses = oatpp::Vector<oatpp::Object<dto::DiagnosisDto>>::createShared();
  c.exec("SELECT * FROM consultation_diagnoses WHERE consultation_id = $1 ORDER BY sort_order, created_at", Params(id))
      .each([&](const Row& x) {
        auto dx = dto::DiagnosisDto::createShared();
        dx->id = x.s("id");
        dx->codeSystem = x.s("code_system");
        dx->code = x.s("code");
        dx->display = x.s("display");
        dx->diagnosisType = x.s("diagnosis_type");
        dx->notes = x.s("notes");
        d->diagnoses->push_back(dx);
      });

  d->addenda = oatpp::Vector<oatpp::Object<dto::AddendumDto>>::createShared();
  c.exec("SELECT a.*, u.full_name AS author FROM consultation_addenda a JOIN users u ON u.id = a.author_id "
         "WHERE a.consultation_id = $1 ORDER BY a.created_at", Params(id))
      .each([&](const Row& x) {
        auto a = dto::AddendumDto::createShared();
        a->id = x.s("id");
        a->authorName = x.s("author");
        a->reason = x.s("reason");
        a->content = x.s("content");
        a->createdAt = x.ts("created_at");
        d->addenda->push_back(a);
      });

  auto rx = c.exec("SELECT * FROM prescriptions WHERE consultation_id = $1", Params(id));
  auto p = dto::PrescriptionDto::createShared();
  p->consultationId = id;
  p->items = oatpp::Vector<oatpp::Object<dto::PrescriptionItemDto>>::createShared();
  p->investigations = oatpp::Vector<oatpp::Object<dto::InvestigationOrderDto>>::createShared();
  if (!rx.empty()) {
    auto pr = rx.first();
    p->id = pr.s("id");
    p->status = pr.s("status");
    p->advice = pr.s("advice");
    p->finalizedAt = pr.ts("finalized_at");
    c.exec("SELECT * FROM prescription_items WHERE prescription_id = $1 ORDER BY sort_order", Params(pr.str("id")))
        .each([&](const Row& x) {
          auto it = dto::PrescriptionItemDto::createShared();
          it->id = x.s("id");
          it->medicationId = x.s("medication_id");
          it->medicineName = x.s("medicine_name");
          it->strength = x.s("strength");
          it->dosageForm = x.s("dosage_form");
          it->dose = x.s("dose");
          it->route = x.s("route");
          it->frequency = x.s("frequency");
          it->durationDays = x.i32("duration_days");
          it->quantity = x.f64("quantity");
          it->instructions = x.s("instructions");
          it->genericCode = x.s("generic_code");
          it->sourceKind = x.s("source_kind");
          it->sourceSnapshotId = x.s("source_snapshot_id");
          it->suggestedDose = x.s("suggested_dose");
          it->suggestedFrequency = x.s("suggested_frequency");
          it->suggestedDurationDays = x.i32("suggested_duration_days");
          p->items->push_back(it);
        });
  }
  c.exec("SELECT * FROM investigation_orders WHERE consultation_id = $1 ORDER BY ordered_at, name", Params(id))
      .each([&](const Row& x) {
        auto o = dto::InvestigationOrderDto::createShared();
        o->id = x.s("id");
        o->investigationId = x.s("investigation_id");
        o->name = x.s("name");
        o->category = x.s("category");
        o->loincCode = x.s("loinc_code");
        o->priority = x.s("priority");
        o->clinicalNotes = x.s("clinical_notes");
        o->status = x.s("status");
        o->orderedAt = x.ts("ordered_at");
        p->investigations->push_back(o);
      });
  d->prescription = p;
  return d;
}

oatpp::Object<dto::ConsultationDto> ClinicalService::startFromAppointment(const AuthContext& ctx, const std::string& appointmentId) {
  if (!ctx.doctorId) throw ApiError::forbidden("Only users with an active doctor profile can start consultations");
  return m_db->tx([&](Conn& c) {
    auto appt = SchedulingService::fetch(c, appointmentId);
    if (std::string(*appt->doctorId) != *ctx.doctorId)
      throw ApiError::forbidden("This appointment is assigned to another doctor");
    auto existing = c.exec("SELECT id FROM consultations WHERE appointment_id = $1", Params(appointmentId));
    if (!existing.empty()) return load(c, existing.first().str("id"), &ctx);  // idempotent resume
    SchedulingService::transition(c, ctx, appointmentId, *appt->status, "IN_CONSULTATION", "Consultation started");
    c.exec("UPDATE queue_tokens SET status = 'IN_CONSULTATION', called_at = coalesce(called_at, now()) WHERE appointment_id = $1",
           Params(appointmentId));
    // Carry forward the most recent past history and current medications as a starting point.
    // The clinic's specialty selects the specialty module (e.g. the dermatology assessment).
    auto id = c.exec("INSERT INTO consultations (appointment_id, patient_id, doctor_id, past_medical_history, current_medications, "
                     "  specialty_code) "
                     "SELECT $1, $2, $3, prev.past_medical_history, prev.current_medications, "
                     "  (SELECT specialty_code FROM facilities WHERE id = cf_current_facility()) FROM (SELECT 1) x "
                     "LEFT JOIN LATERAL (SELECT past_medical_history, current_medications FROM consultations "
                     "  WHERE patient_id = $2 AND status = 'FINALIZED' ORDER BY finalized_at DESC LIMIT 1) prev ON true "
                     "RETURNING id",
                     Params(appointmentId, std::string(*appt->patientId), *ctx.doctorId))
                  .first()
                  .str("id");
    c.exec("INSERT INTO prescriptions (consultation_id, patient_id, doctor_id) VALUES ($1, $2, $3)",
           Params(id, std::string(*appt->patientId), *ctx.doctorId));
    m_audit->record(c, ctx, AuditEvent{"CONSULTATION_START", "consultation", id, std::string(*appt->patientId), "SUCCESS",
                                       {{"appointmentId", appointmentId}}});
    return load(c, id, &ctx);
  });
}

oatpp::Object<dto::ConsultationDto> ClinicalService::get(const AuthContext& ctx, const std::string& id, bool audit) const {
  return m_db->tx([&](Conn& c) {
    auto d = load(c, id, &ctx);
    if (audit) m_audit->record(c, ctx, AuditEvent{"CONSULTATION_VIEW", "consultation", id, std::string(*d->patientId), "SUCCESS", {}});
    return d;
  });
}

oatpp::Object<dto::ConsultationDto> ClinicalService::loadForExport(const std::string& id) const {
  return m_db->withConn([&](Conn& c) { return load(c, id, nullptr); });
}

Row ClinicalService::requireOwnDraft(Conn& c, const AuthContext& ctx, const std::string& id, const oatpp::Int32& version) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Consultation");
  auto r = c.exec("SELECT * FROM consultations WHERE id = $1 FOR UPDATE", Params(id));
  if (r.empty()) throw ApiError::notFound("Consultation");
  auto row = r.first();
  if (!ctx.doctorId || row.str("doctor_id") != *ctx.doctorId)
    throw ApiError::forbidden("Only the treating doctor can edit this consultation");
  if (row.str("status") != "DRAFT")
    throw ApiError::conflict("RECORD_LOCKED", "Finalized consultations cannot be edited. Add an addendum instead.");
  if (version && row.i64v("version") != *version)
    throw ApiError::conflict("STALE_VERSION", "Consultation was saved elsewhere. Reload to continue.");
  return row;
}

static void validateVitals(Validator& v, const oatpp::Object<dto::VitalsDto>& x) {
  auto rng = [&](const auto& val, double lo, double hi, const char* field, const char* label) {
    if (val) v.check(*val >= lo && *val <= hi, std::string("vitals.") + field,
                     std::string(label) + " must be between " + std::to_string((int)lo) + " and " + std::to_string((int)hi));
  };
  rng(x->bpSystolic, 40, 300, "bpSystolic", "Systolic BP");
  rng(x->bpDiastolic, 20, 200, "bpDiastolic", "Diastolic BP");
  rng(x->pulse, 20, 250, "pulse", "Pulse");
  rng(x->temperatureC, 30, 45, "temperatureC", "Temperature (°C)");
  rng(x->respiratoryRate, 4, 80, "respiratoryRate", "Respiratory rate");
  rng(x->spo2, 50, 100, "spo2", "SpO2");
  rng(x->heightCm, 30, 250, "heightCm", "Height (cm)");
  rng(x->weightKg, 1, 400, "weightKg", "Weight (kg)");
  if (x->bpSystolic && x->bpDiastolic)
    v.check(*x->bpSystolic > *x->bpDiastolic, "vitals.bpDiastolic", "Diastolic must be lower than systolic");
  if ((x->bpSystolic && !x->bpDiastolic) || (!x->bpSystolic && x->bpDiastolic))
    v.fail("vitals.bpDiastolic", "Enter both systolic and diastolic values");
}

oatpp::Object<dto::ConsultationDto> ClinicalService::updateDraft(const AuthContext& ctx, const std::string& id,
                                                                 const oatpp::Object<dto::ConsultationUpdateDto>& in) {
  Validator v;
  auto fud = opt(in->followUpDate);
  if (fud) v.check(text::isIsoDate(*fud), "followUpDate", "Use YYYY-MM-DD");
  for (auto* f : {&in->chiefComplaint, &in->historyPresentIllness, &in->pastMedicalHistory, &in->currentMedications,
                  &in->examinationNotes, &in->treatmentPlan, &in->followUpInstructions})
    if (*f && (*f)->size() > 8000) v.fail("notes", "Text fields are limited to 8000 characters");
  if (in->vitals) validateVitals(v, in->vitals);
  if (in->diagnoses) {
    v.check(in->diagnoses->size() <= 20, "diagnoses", "At most 20 diagnoses");
    int i = 0;
    for (auto& dx : *in->diagnoses) {
      std::string k = "diagnoses[" + std::to_string(i++) + "]";
      std::string sys = trimmed(dx->codeSystem);
      if (sys.empty()) sys = "FREE_TEXT";
      v.check(text::oneOf(sys, {"ICD-10", "SNOMED-CT", "FREE_TEXT"}), k + ".codeSystem", "ICD-10, SNOMED-CT or FREE_TEXT");
      v.check(trimmed(dx->display).size() >= 2, k + ".display", "Diagnosis text is required");
      if (sys != "FREE_TEXT") v.check(!trimmed(dx->code).empty(), k + ".code", "Code is required for coded diagnoses");
      std::string type = trimmed(dx->diagnosisType);
      if (!type.empty()) v.check(text::oneOf(type, {"PRIMARY", "SECONDARY", "PROVISIONAL"}), k + ".diagnosisType", "Invalid type");
    }
  }
  v.throwIfAny();
  if (fud) {
    bool future = m_db->withConn([&](Conn& c) { return c.exec("SELECT $1::date >= current_date AS ok", Params(*fud)).first().boolv("ok"); });
    if (!future) throw ApiError::badRequest("Follow-up date must be today or later", {{"followUpDate", "Must be in the future"}});
  }

  return m_db->tx([&](Conn& c) {
    auto row = requireOwnDraft(c, ctx, id, in->version);
    std::string patientId = row.str("patient_id");
    c.exec("UPDATE consultations SET chief_complaint = $2, history_present_illness = $3, past_medical_history = $4, "
           "current_medications = $5, examination_notes = $6, treatment_plan = $7, follow_up_instructions = $8, "
           "follow_up_date = $9::date, version = version + 1 WHERE id = $1",
           Params(id, opt(in->chiefComplaint), opt(in->historyPresentIllness), opt(in->pastMedicalHistory),
                  opt(in->currentMedications), opt(in->examinationNotes), opt(in->treatmentPlan),
                  opt(in->followUpInstructions), fud));
    nlohmann::json sections = nlohmann::json::array({"notes"});
    if (in->vitals) {
      auto& x = in->vitals;
      c.exec("INSERT INTO vital_signs (consultation_id, patient_id, recorded_by, bp_systolic, bp_diastolic, pulse, temperature_c, "
             "respiratory_rate, spo2, height_cm, weight_kg) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) "
             "ON CONFLICT (consultation_id) DO UPDATE SET bp_systolic = EXCLUDED.bp_systolic, bp_diastolic = EXCLUDED.bp_diastolic, "
             "pulse = EXCLUDED.pulse, temperature_c = EXCLUDED.temperature_c, respiratory_rate = EXCLUDED.respiratory_rate, "
             "spo2 = EXCLUDED.spo2, height_cm = EXCLUDED.height_cm, weight_kg = EXCLUDED.weight_kg, recorded_at = now(), "
             "recorded_by = EXCLUDED.recorded_by",
             Params(id, patientId, ctx.userId, x->bpSystolic, x->bpDiastolic, x->pulse, x->temperatureC, x->respiratoryRate,
                    x->spo2, x->heightCm, x->weightKg));
      sections.push_back("vitals");
    }
    if (in->diagnoses) {
      c.exec("DELETE FROM consultation_diagnoses WHERE consultation_id = $1", Params(id));
      int order = 0;
      for (auto& dx : *in->diagnoses) {
        std::string sys = trimmed(dx->codeSystem);
        if (sys.empty()) sys = "FREE_TEXT";
        std::string type = trimmed(dx->diagnosisType);
        if (type.empty()) type = order == 0 ? "PRIMARY" : "SECONDARY";
        c.exec("INSERT INTO consultation_diagnoses (consultation_id, code_system, code, display, diagnosis_type, notes, sort_order) "
               "VALUES ($1,$2,$3,$4,$5,$6,$7)",
               Params(id, sys, sys == "FREE_TEXT" ? std::optional<std::string>() : opt(dx->code), trimmed(dx->display), type,
                      opt(dx->notes), order++));
      }
      sections.push_back("diagnoses");
    }
    m_audit->record(c, ctx, AuditEvent{"CONSULTATION_UPDATE", "consultation", id, patientId, "SUCCESS", {{"sections", sections}}});
    return load(c, id, &ctx);
  });
}

oatpp::Object<dto::ConsultationDto> ClinicalService::updatePrescription(const AuthContext& ctx, const std::string& id,
                                                                        const oatpp::Object<dto::PrescriptionUpdateDto>& in) {
  Validator v;
  if (in->items) {
    v.check(in->items->size() <= 30, "items", "At most 30 medicines");
    int i = 0;
    for (auto& it : *in->items) {
      std::string k = "items[" + std::to_string(i++) + "]";
      v.check(trimmed(it->medicineName).size() >= 2, k + ".medicineName", "Medicine is required");
      v.check(!trimmed(it->dose).empty(), k + ".dose", "Dose is required");
      v.check(!trimmed(it->route).empty(), k + ".route", "Route is required");
      v.check(!trimmed(it->frequency).empty(), k + ".frequency", "Frequency is required");
      if (it->durationDays) v.check(*it->durationDays >= 1 && *it->durationDays <= 365, k + ".durationDays", "1 to 365 days");
      if (it->quantity) v.check(*it->quantity > 0 && *it->quantity <= 10000, k + ".quantity", "Must be positive");
      if (it->medicationId) v.check(text::isUuid(*it->medicationId), k + ".medicationId", "Invalid catalogue reference");
      std::string sk = trimmed(it->sourceKind);
      if (!sk.empty()) v.check(text::oneOf(sk, {"MANUAL", "CLINICAL", "MY_PATTERNS", "NETWORK"}), k + ".sourceKind", "Invalid source");
      if (it->sourceSnapshotId) v.check(text::isUuid(*it->sourceSnapshotId), k + ".sourceSnapshotId", "Invalid reference");
      if (it->suggestedDurationDays) v.check(*it->suggestedDurationDays >= 1 && *it->suggestedDurationDays <= 365, k + ".suggestedDurationDays", "1 to 365 days");
    }
  }
  if (in->investigations) {
    v.check(in->investigations->size() <= 30, "investigations", "At most 30 orders");
    int i = 0;
    for (auto& o : *in->investigations) {
      std::string k = "investigations[" + std::to_string(i++) + "]";
      bool fromCatalogue = o->investigationId && text::isUuid(*o->investigationId);
      if (!fromCatalogue) {
        v.check(trimmed(o->name).size() >= 2, k + ".name", "Investigation name is required");
        v.check(text::oneOf(trimmed(o->category), {"LAB", "RADIOLOGY", "OTHER"}), k + ".category", "LAB, RADIOLOGY or OTHER");
      }
      std::string pr = trimmed(o->priority);
      if (!pr.empty()) v.check(text::oneOf(pr, {"ROUTINE", "URGENT"}), k + ".priority", "ROUTINE or URGENT");
    }
  }
  if (in->advice && in->advice->size() > 4000) v.fail("advice", "Advice is limited to 4000 characters");
  v.throwIfAny();

  return m_db->tx([&](Conn& c) {
    if (!ctx.has(perm::PRESCRIPTION_WRITE)) throw ApiError::forbidden();
    auto row = requireOwnDraft(c, ctx, id, nullptr);
    std::string patientId = row.str("patient_id");
    auto rxId = c.exec("SELECT id FROM prescriptions WHERE consultation_id = $1", Params(id)).first().str("id");
    c.exec("UPDATE prescriptions SET advice = $2 WHERE id = $1", Params(rxId, opt(in->advice)));
    if (in->items) {
      c.exec("DELETE FROM prescription_items WHERE prescription_id = $1", Params(rxId));
      int order = 0;
      for (auto& it : *in->items) {
        std::string sk = trimmed(it->sourceKind);
        if (sk.empty()) sk = "MANUAL";
        auto snap = opt(it->sourceSnapshotId);
        if (sk != "MANUAL") {
          // A suggestion-sourced item must point at a panel the doctor actually saw for this consultation.
          if (!snap || c.exec("SELECT 1 FROM suggestion_snapshots WHERE id = $1 AND consultation_id = $2", Params(*snap, id)).empty())
            throw ApiError::badRequest("Suggestion source does not belong to this consultation");
        } else {
          snap.reset();
        }
        // generic_code is taken from the catalogue, never from the client.
        c.exec("INSERT INTO prescription_items (prescription_id, medication_id, medicine_name, strength, dosage_form, dose, route, "
               "frequency, duration_days, quantity, instructions, sort_order, generic_code, source_kind, source_snapshot_id, "
               "suggested_dose, suggested_frequency, suggested_duration_days) "
               "VALUES ($1,$2::uuid,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,"
               "  (SELECT generic_code FROM medication_catalogue WHERE id = $2::uuid),$13,$14::uuid,$15,$16,$17)",
               Params(rxId, opt(it->medicationId), trimmed(it->medicineName), opt(it->strength), opt(it->dosageForm),
                      trimmed(it->dose), text::upper(trimmed(it->route)), trimmed(it->frequency), it->durationDays, it->quantity,
                      opt(it->instructions), order++, sk, snap, sk == "MANUAL" ? std::nullopt : opt(it->suggestedDose),
                      sk == "MANUAL" ? std::nullopt : opt(it->suggestedFrequency),
                      sk == "MANUAL" ? oatpp::Int32(nullptr) : it->suggestedDurationDays));
      }
    }
    if (in->investigations) {
      c.exec("DELETE FROM investigation_orders WHERE consultation_id = $1", Params(id));
      for (auto& o : *in->investigations) {
        std::string pr = trimmed(o->priority);
        if (pr.empty()) pr = "ROUTINE";
        if (o->investigationId && text::isUuid(*o->investigationId)) {
          auto r = c.exec("INSERT INTO investigation_orders (consultation_id, patient_id, doctor_id, investigation_id, name, category, "
                          "loinc_code, priority, clinical_notes) SELECT $1, $2, $3, id, name, category, loinc_code, $5, $6 "
                          "FROM investigation_catalogue WHERE id = $4 AND is_active RETURNING id",
                          Params(id, patientId, *ctx.doctorId, std::string(*o->investigationId), pr, opt(o->clinicalNotes)));
          if (r.empty()) throw ApiError::badRequest("Unknown investigation in catalogue");
        } else {
          c.exec("INSERT INTO investigation_orders (consultation_id, patient_id, doctor_id, name, category, priority, clinical_notes) "
                 "VALUES ($1,$2,$3,$4,$5,$6,$7)",
                 Params(id, patientId, *ctx.doctorId, trimmed(o->name), trimmed(o->category), pr, opt(o->clinicalNotes)));
        }
      }
    }
    m_audit->record(c, ctx, AuditEvent{"PRESCRIPTION_UPDATE", "prescription", rxId, patientId, "SUCCESS",
                                       {{"consultationId", id},
                                        {"medicines", in->items ? (int)in->items->size() : -1},
                                        {"investigations", in->investigations ? (int)in->investigations->size() : -1}}});
    return load(c, id, &ctx);
  });
}

oatpp::Object<dto::ConsultationDto> ClinicalService::finalize(const AuthContext& ctx, const std::string& id,
                                                              const FinalizeOptions& opts) {
  return m_db->tx([&](Conn& c) {
    auto row = requireOwnDraft(c, ctx, id, nullptr);
    std::string patientId = row.str("patient_id");
    Validator v;
    v.check(!row.str("chief_complaint").empty(), "chiefComplaint", "Chief complaint is required before finalizing");
    auto dxCount = c.exec("SELECT count(*) AS n FROM consultation_diagnoses WHERE consultation_id = $1", Params(id)).first().i64v("n");
    v.check(dxCount > 0, "diagnoses", "At least one diagnosis is required before finalizing");
    v.throwIfAny("Consultation is incomplete");
    for (auto& ext : m_extensions) ext->beforeFinalize(c, ctx, id, opts);

    c.exec("UPDATE prescriptions SET status = 'FINALIZED', finalized_at = now() WHERE consultation_id = $1", Params(id));
    c.exec("UPDATE consultations SET status = 'FINALIZED', finalized_at = now(), finalized_by = $2, version = version + 1 "
           "WHERE id = $1", Params(id, ctx.userId));
    if (!row.isNull("appointment_id")) {
      std::string apptId = row.str("appointment_id");
      auto st = c.exec("SELECT status FROM appointments WHERE id = $1", Params(apptId)).first().str("status");
      if (st == "IN_CONSULTATION") SchedulingService::transition(c, ctx, apptId, st, "COMPLETED", "Consultation finalized");
      c.exec("UPDATE queue_tokens SET status = 'DONE', completed_at = now() WHERE appointment_id = $1", Params(apptId));
    }
    for (auto& ext : m_extensions) ext->afterFinalize(c, ctx, id);
    m_audit->record(c, ctx, AuditEvent{"CONSULTATION_FINALIZE", "consultation", id, patientId, "SUCCESS",
                                       {{"safetyOverride", opts.safetyOverrideReason.has_value()}}});
    return load(c, id, &ctx);
  });
}

oatpp::Object<dto::ConsultationDto> ClinicalService::addAddendum(const AuthContext& ctx, const std::string& id,
                                                                 const oatpp::Object<dto::AddendumInputDto>& in) {
  Validator v;
  std::string reason = trimmed(in->reason), content = trimmed(in->content);
  v.check(reason.size() >= 3, "reason", "Reason for correction is required");
  v.check(content.size() >= 3 && content.size() <= 8000, "content", "Addendum text is required");
  v.throwIfAny();
  return m_db->tx([&](Conn& c) {
    if (!text::isUuid(id)) throw ApiError::notFound("Consultation");
    auto r = c.exec("SELECT status, patient_id FROM consultations WHERE id = $1", Params(id));
    if (r.empty()) throw ApiError::notFound("Consultation");
    if (r.first().str("status") != "FINALIZED")
      throw ApiError::conflict("NOT_FINALIZED", "Draft consultations are edited directly; addenda apply to finalized records");
    if (!ctx.doctorId) throw ApiError::forbidden("Only doctors can add clinical addenda");
    auto aid = c.exec("INSERT INTO consultation_addenda (consultation_id, author_id, reason, content) VALUES ($1,$2,$3,$4) RETURNING id",
                      Params(id, ctx.userId, reason, content)).first().str("id");
    m_audit->record(c, ctx, AuditEvent{"CONSULTATION_ADDENDUM", "consultation", id, r.first().str("patient_id"), "SUCCESS",
                                       {{"addendumId", aid}, {"reason", reason}}});
    return load(c, id, &ctx);
  });
}

oatpp::Vector<oatpp::Object<dto::TimelineEntryDto>> ClinicalService::timeline(const AuthContext& ctx, const std::string& patientId) const {
  if (!text::isUuid(patientId)) throw ApiError::notFound("Patient");
  return m_db->tx([&](Conn& c) {
    if (c.exec("SELECT 1 FROM patients WHERE id = $1", Params(patientId)).empty()) throw ApiError::notFound("Patient");
    auto out = oatpp::Vector<oatpp::Object<dto::TimelineEntryDto>>::createShared();
    c.exec("SELECT c.id, c.started_at, c.status, c.chief_complaint, c.follow_up_date::text AS fud, d.display_name, "
           "  coalesce((SELECT array_agg(coalesce(x.code || ' ', '') || x.display ORDER BY x.sort_order) FROM consultation_diagnoses x "
           "            WHERE x.consultation_id = c.id), '{}') AS dx, "
           "  coalesce((SELECT array_agg(i.medicine_name || coalesce(' ' || i.strength, '') || ' — ' || i.dose || ' ' || i.frequency "
           "            || coalesce(' x ' || i.duration_days || 'd', '') ORDER BY i.sort_order) "
           "            FROM prescription_items i JOIN prescriptions rx ON rx.id = i.prescription_id WHERE rx.consultation_id = c.id), '{}') AS meds, "
           "  coalesce((SELECT array_agg(o.name ORDER BY o.name) FROM investigation_orders o WHERE o.consultation_id = c.id "
           "            AND o.status <> 'CANCELLED'), '{}') AS inv, v.* "
           "FROM consultations c JOIN doctors d ON d.id = c.doctor_id LEFT JOIN vital_signs v ON v.consultation_id = c.id "
           "WHERE c.patient_id = $1 ORDER BY c.started_at DESC LIMIT 200",
           Params(patientId))
        .each([&](const Row& r) {
          auto t = dto::TimelineEntryDto::createShared();
          t->consultationId = r.s("id");
          t->startedAt = r.ts("started_at");
          t->status = r.s("status");
          t->doctorName = r.s("display_name");
          t->chiefComplaint = r.s("chief_complaint");
          t->diagnoses = r.strArray("dx");
          t->medicines = r.strArray("meds");
          t->investigations = r.strArray("inv");
          t->followUpDate = r.s("fud");
          if (!r.isNull("consultation_id")) t->vitals = vitalsFromRow(r);
          out->push_back(t);
        });
    m_audit->record(c, ctx, AuditEvent{"PATIENT_CLINICAL_HISTORY_VIEW", "patient", patientId, patientId, "SUCCESS",
                                       {{"entries", (int)out->size()}}});
    return out;
  });
}

// ---------------- Catalogues ----------------
static oatpp::Object<dto::MedicationDto> medFromRow(const Row& r) {
  auto m = dto::MedicationDto::createShared();
  m->id = r.s("id");
  m->genericName = r.s("generic_name");
  m->brandName = r.s("brand_name");
  m->strength = r.s("strength");
  m->dosageForm = r.s("dosage_form");
  m->defaultRoute = r.s("default_route");
  m->drugSchedule = r.s("drug_schedule");
  m->isActive = r.b("is_active");
  m->genericCode = r.s("generic_code");
  m->category = r.s("category");
  m->reviewStatus = r.s("review_status");
  m->highRisk = r.b("high_risk");
  if (r.str("review_status") == "APPROVED") {
    m->defaultDose = r.s("default_dose");
    m->defaultFrequency = r.s("default_frequency");
    m->defaultDurationDays = r.i32("default_duration_days");
  }
  return m;
}

oatpp::Vector<oatpp::Object<dto::MedicationDto>> ClinicalService::searchMedications(const std::string& q, bool includeInactive) const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::MedicationDto>>::createShared();
    c.exec("SELECT * FROM medication_catalogue WHERE ($2 OR is_active) AND ($1 = '' OR "
           "lower(generic_name || ' ' || coalesce(brand_name, '')) LIKE '%' || lower($1) || '%' OR "
           "similarity(lower(generic_name || ' ' || coalesce(brand_name, '')), lower($1)) > 0.25) "
           "ORDER BY (lower(generic_name) LIKE lower($1) || '%') DESC, generic_name, strength LIMIT 50",
           Params(text::trim(q), includeInactive))
        .each([&](const Row& r) { out->push_back(medFromRow(r)); });
    return out;
  });
}

oatpp::Object<dto::MedicationDto> ClinicalService::saveMedication(const AuthContext& ctx, const std::string& id,
                                                                  const oatpp::Object<dto::MedicationDto>& in) {
  Validator v;
  v.check(trimmed(in->genericName).size() >= 2, "genericName", "Generic name is required");
  v.check(!trimmed(in->strength).empty(), "strength", "Strength is required");
  v.check(!trimmed(in->dosageForm).empty(), "dosageForm", "Dosage form is required");
  v.throwIfAny();
  return m_db->tx([&](Conn& c) {
    auto genericCode = opt(in->genericCode);
    if (genericCode && c.exec("SELECT 1 FROM medication_catalogue WHERE generic_code = $1 LIMIT 1", Params(*genericCode)).empty())
      throw ApiError::badRequest("Unknown generic code", {{"genericCode", "Pick an existing generic medicine"}});
    // New products (e.g. a clinic's brands) share the generic's coded identity, but their
    // clinical information starts UNREVIEWED.
    Result r = id.empty()
                   ? c.exec("INSERT INTO medication_catalogue (generic_name, brand_name, strength, dosage_form, default_route, drug_schedule, "
                            "  generic_code, ingredient_codes, category, routes) "
                            "SELECT $1,$2,$3,$4,coalesce($5,'ORAL'),$6,$7, "
                            "  coalesce((SELECT ingredient_codes FROM medication_catalogue WHERE generic_code = $7 LIMIT 1), "
                            "           CASE WHEN $7::text IS NULL THEN '{}'::text[] ELSE ARRAY[$7::text] END), "
                            "  (SELECT category FROM medication_catalogue WHERE generic_code = $7 LIMIT 1), "
                            "  ARRAY[coalesce($5,'ORAL')] RETURNING *",
                            Params(trimmed(in->genericName), opt(in->brandName), trimmed(in->strength),
                                   text::upper(trimmed(in->dosageForm)), opt(in->defaultRoute), opt(in->drugSchedule), genericCode))
                   : c.exec("UPDATE medication_catalogue SET generic_name=$2, brand_name=$3, strength=$4, dosage_form=$5, "
                            "default_route=coalesce($6, default_route), drug_schedule=$7, is_active=coalesce($8, is_active) "
                            "WHERE id = $1 RETURNING *",
                            Params(id, trimmed(in->genericName), opt(in->brandName), trimmed(in->strength),
                                   text::upper(trimmed(in->dosageForm)), opt(in->defaultRoute), opt(in->drugSchedule), in->isActive));
    if (r.empty()) throw ApiError::notFound("Medication");
    auto m = medFromRow(r.first());
    m_audit->record(c, ctx, AuditEvent{id.empty() ? "CATALOGUE_MEDICATION_CREATE" : "CATALOGUE_MEDICATION_UPDATE",
                                       "medication", *m->id, std::nullopt, "SUCCESS", {}});
    return m;
  });
}

oatpp::Vector<oatpp::Object<dto::InvestigationCatalogueDto>> ClinicalService::searchInvestigations(const std::string& q) const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::InvestigationCatalogueDto>>::createShared();
    c.exec("SELECT * FROM investigation_catalogue WHERE is_active AND ($1 = '' OR lower(name) LIKE '%' || lower($1) || '%' "
           "OR lower(code) = lower($1)) ORDER BY category, name LIMIT 50",
           Params(text::trim(q)))
        .each([&](const Row& r) {
          auto i = dto::InvestigationCatalogueDto::createShared();
          i->id = r.s("id");
          i->code = r.s("code");
          i->name = r.s("name");
          i->category = r.s("category");
          i->loincCode = r.s("loinc_code");
          i->specimen = r.s("specimen");
          i->isActive = r.b("is_active");
          out->push_back(i);
        });
    return out;
  });
}

oatpp::Vector<oatpp::Object<dto::DiagnosisCodeDto>> ClinicalService::searchDiagnoses(const std::string& q) const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::DiagnosisCodeDto>>::createShared();
    c.exec("SELECT * FROM diagnosis_codes WHERE $1 = '' OR lower(display) LIKE '%' || lower($1) || '%' "
           "OR lower(code) LIKE lower($1) || '%' OR similarity(lower(display), lower($1)) > 0.3 "
           "ORDER BY (lower(code) LIKE lower($1) || '%') DESC, display LIMIT 30",
           Params(text::trim(q)))
        .each([&](const Row& r) {
          auto d = dto::DiagnosisCodeDto::createShared();
          d->codeSystem = r.s("code_system");
          d->code = r.s("code");
          d->display = r.s("display");
          out->push_back(d);
        });
    return out;
  });
}

}  // namespace cf
