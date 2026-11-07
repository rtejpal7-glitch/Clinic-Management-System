#pragma once
// OPD consultations / EMR, e-prescriptions and CPOE investigation orders
// (NABH CMS COP.1 scope). Clinical decision support is deliberately NOT
// implemented here; see ClinicalDecisionSupport extension point below.

#include <memory>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "dto/ClinicalDtos.hpp"
#include "security/AuthContext.hpp"

namespace cf {

// Extension point for a future, separately validated CDS service (drug-drug
// interaction, allergy cross-checks, dose limits). The PoC registers no
// implementation, and the UI states that no automated checks are performed.
class ClinicalDecisionSupport {
 public:
  virtual ~ClinicalDecisionSupport() = default;
  virtual std::vector<std::string> checkPrescription(const std::string& patientId,
                                                     const oatpp::Object<dto::PrescriptionUpdateDto>& rx) = 0;
};

class ClinicalService {
 public:
  ClinicalService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit)
      : m_db(std::move(db)), m_audit(std::move(audit)) {}

  oatpp::Object<dto::ConsultationDto> startFromAppointment(const AuthContext& ctx, const std::string& appointmentId);
  oatpp::Object<dto::ConsultationDto> get(const AuthContext& ctx, const std::string& id, bool audit = true) const;
  oatpp::Object<dto::ConsultationDto> updateDraft(const AuthContext& ctx, const std::string& id,
                                                  const oatpp::Object<dto::ConsultationUpdateDto>& in);
  oatpp::Object<dto::ConsultationDto> updatePrescription(const AuthContext& ctx, const std::string& id,
                                                         const oatpp::Object<dto::PrescriptionUpdateDto>& in);
  oatpp::Object<dto::ConsultationDto> finalize(const AuthContext& ctx, const std::string& id);
  oatpp::Object<dto::ConsultationDto> addAddendum(const AuthContext& ctx, const std::string& id,
                                                  const oatpp::Object<dto::AddendumInputDto>& in);
  oatpp::Vector<oatpp::Object<dto::TimelineEntryDto>> timeline(const AuthContext& ctx, const std::string& patientId) const;

  // Catalogues
  oatpp::Vector<oatpp::Object<dto::MedicationDto>> searchMedications(const std::string& q, bool includeInactive) const;
  oatpp::Object<dto::MedicationDto> saveMedication(const AuthContext& ctx, const std::string& id,
                                                   const oatpp::Object<dto::MedicationDto>& in);
  oatpp::Vector<oatpp::Object<dto::InvestigationCatalogueDto>> searchInvestigations(const std::string& q) const;
  oatpp::Vector<oatpp::Object<dto::DiagnosisCodeDto>> searchDiagnoses(const std::string& q) const;

  // Used by the ABDM/FHIR layer (no audit; caller audits)
  oatpp::Object<dto::ConsultationDto> loadForExport(const std::string& id) const;

 private:
  oatpp::Object<dto::ConsultationDto> load(Conn& c, const std::string& id, const AuthContext* ctx) const;
  Row requireOwnDraft(Conn& c, const AuthContext& ctx, const std::string& id, const oatpp::Int32& version) const;
  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
};

}  // namespace cf
