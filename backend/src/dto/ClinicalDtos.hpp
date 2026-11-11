#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

class VitalsDto : public oatpp::DTO {
  DTO_INIT(VitalsDto, DTO)
  DTO_FIELD(Int32, bpSystolic);
  DTO_FIELD(Int32, bpDiastolic);
  DTO_FIELD(Int32, pulse);
  DTO_FIELD(Float64, temperatureC);
  DTO_FIELD(Int32, respiratoryRate);
  DTO_FIELD(Int32, spo2);
  DTO_FIELD(Float64, heightCm);
  DTO_FIELD(Float64, weightKg);
  DTO_FIELD(Float64, bmi);          // computed, read-only
  DTO_FIELD(String, recordedAt);
};

class DiagnosisDto : public oatpp::DTO {
  DTO_INIT(DiagnosisDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, codeSystem);    // ICD-10 | SNOMED-CT | FREE_TEXT
  DTO_FIELD(String, code);
  DTO_FIELD(String, display);
  DTO_FIELD(String, diagnosisType); // PRIMARY | SECONDARY | PROVISIONAL
  DTO_FIELD(String, notes);
};

class AddendumDto : public oatpp::DTO {
  DTO_INIT(AddendumDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, authorName);
  DTO_FIELD(String, reason);
  DTO_FIELD(String, content);
  DTO_FIELD(String, createdAt);
};

class PrescriptionItemDto : public oatpp::DTO {
  DTO_INIT(PrescriptionItemDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, medicationId);
  DTO_FIELD(String, medicineName);
  DTO_FIELD(String, strength);
  DTO_FIELD(String, dosageForm);
  DTO_FIELD(String, dose);
  DTO_FIELD(String, route);
  DTO_FIELD(String, frequency);
  DTO_FIELD(Int32, durationDays);
  DTO_FIELD(Float64, quantity);
  DTO_FIELD(String, instructions);
  // Decision-support provenance: MANUAL (typed by the doctor) or the panel the doctor
  // explicitly added it from. Suggested values are kept so modifications are visible.
  DTO_FIELD(String, genericCode);
  DTO_FIELD(String, sourceKind);         // MANUAL | CLINICAL | MY_PATTERNS | NETWORK
  DTO_FIELD(String, sourceSnapshotId);
  DTO_FIELD(String, suggestedDose);
  DTO_FIELD(String, suggestedFrequency);
  DTO_FIELD(Int32, suggestedDurationDays);
};

class InvestigationOrderDto : public oatpp::DTO {
  DTO_INIT(InvestigationOrderDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, investigationId);
  DTO_FIELD(String, name);
  DTO_FIELD(String, category);
  DTO_FIELD(String, loincCode);
  DTO_FIELD(String, priority);
  DTO_FIELD(String, clinicalNotes);
  DTO_FIELD(String, status);
  DTO_FIELD(String, orderedAt);
};

class PrescriptionDto : public oatpp::DTO {
  DTO_INIT(PrescriptionDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, consultationId);
  DTO_FIELD(String, status);
  DTO_FIELD(String, advice);
  DTO_FIELD(String, finalizedAt);
  DTO_FIELD(Vector<Object<PrescriptionItemDto>>, items);
  DTO_FIELD(Vector<Object<InvestigationOrderDto>>, investigations);
};

class PrescriptionUpdateDto : public oatpp::DTO {
  DTO_INIT(PrescriptionUpdateDto, DTO)
  DTO_FIELD(String, advice);
  DTO_FIELD(Vector<Object<PrescriptionItemDto>>, items);
  DTO_FIELD(Vector<Object<InvestigationOrderDto>>, investigations);
};

class ConsultationDto : public oatpp::DTO {
  DTO_INIT(ConsultationDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, appointmentId);
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, patientName);
  DTO_FIELD(String, patientNumber);
  DTO_FIELD(String, doctorId);
  DTO_FIELD(String, doctorName);
  DTO_FIELD(String, doctorQualification);
  DTO_FIELD(String, doctorRegistration);
  DTO_FIELD(String, status);
  DTO_FIELD(String, chiefComplaint);
  DTO_FIELD(String, historyPresentIllness);
  DTO_FIELD(String, pastMedicalHistory);
  DTO_FIELD(String, currentMedications);
  DTO_FIELD(String, examinationNotes);
  DTO_FIELD(String, treatmentPlan);
  DTO_FIELD(String, followUpInstructions);
  DTO_FIELD(String, followUpDate);
  DTO_FIELD(String, startedAt);
  DTO_FIELD(String, finalizedAt);
  DTO_FIELD(Int32, version);
  DTO_FIELD(Object<VitalsDto>, vitals);
  DTO_FIELD(Vector<Object<DiagnosisDto>>, diagnoses);
  DTO_FIELD(Vector<Object<AddendumDto>>, addenda);
  DTO_FIELD(Object<PrescriptionDto>, prescription);
  DTO_FIELD(Boolean, canEdit);
  DTO_FIELD(String, specialtyCode);      // GENERAL | DERMATOLOGY
};

class FinalizeRequestDto : public oatpp::DTO {
  DTO_INIT(FinalizeRequestDto, DTO)
  DTO_FIELD(String, safetyOverrideReason);
};

class ConsultationUpdateDto : public oatpp::DTO {
  DTO_INIT(ConsultationUpdateDto, DTO)
  DTO_FIELD(Int32, version);
  DTO_FIELD(String, chiefComplaint);
  DTO_FIELD(String, historyPresentIllness);
  DTO_FIELD(String, pastMedicalHistory);
  DTO_FIELD(String, currentMedications);
  DTO_FIELD(String, examinationNotes);
  DTO_FIELD(String, treatmentPlan);
  DTO_FIELD(String, followUpInstructions);
  DTO_FIELD(String, followUpDate);
  DTO_FIELD(Object<VitalsDto>, vitals);
  DTO_FIELD(Vector<Object<DiagnosisDto>>, diagnoses);
};

class AddendumInputDto : public oatpp::DTO {
  DTO_INIT(AddendumInputDto, DTO)
  DTO_FIELD(String, reason);
  DTO_FIELD(String, content);
};

class TimelineEntryDto : public oatpp::DTO {
  DTO_INIT(TimelineEntryDto, DTO)
  DTO_FIELD(String, consultationId);
  DTO_FIELD(String, startedAt);
  DTO_FIELD(String, status);
  DTO_FIELD(String, doctorName);
  DTO_FIELD(String, chiefComplaint);
  DTO_FIELD(Vector<String>, diagnoses);
  DTO_FIELD(Vector<String>, medicines);
  DTO_FIELD(Vector<String>, investigations);
  DTO_FIELD(Object<VitalsDto>, vitals);
  DTO_FIELD(String, followUpDate);
};

// ---------- Catalogues ----------
class MedicationDto : public oatpp::DTO {
  DTO_INIT(MedicationDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, genericName);
  DTO_FIELD(String, brandName);
  DTO_FIELD(String, strength);
  DTO_FIELD(String, dosageForm);
  DTO_FIELD(String, defaultRoute);
  DTO_FIELD(String, drugSchedule);
  DTO_FIELD(Boolean, isActive);
  DTO_FIELD(String, genericCode);
  DTO_FIELD(String, category);
  DTO_FIELD(String, reviewStatus);       // UNREVIEWED | APPROVED | RETIRED (clinical information)
  DTO_FIELD(Boolean, highRisk);
  // Defaults are returned only when the clinical information is APPROVED
  DTO_FIELD(String, defaultDose);
  DTO_FIELD(String, defaultFrequency);
  DTO_FIELD(Int32, defaultDurationDays);
};

class InvestigationCatalogueDto : public oatpp::DTO {
  DTO_INIT(InvestigationCatalogueDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, code);
  DTO_FIELD(String, name);
  DTO_FIELD(String, category);
  DTO_FIELD(String, loincCode);
  DTO_FIELD(String, specimen);
  DTO_FIELD(Boolean, isActive);
};

class DiagnosisCodeDto : public oatpp::DTO {
  DTO_INIT(DiagnosisCodeDto, DTO)
  DTO_FIELD(String, codeSystem);
  DTO_FIELD(String, code);
  DTO_FIELD(String, display);
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
