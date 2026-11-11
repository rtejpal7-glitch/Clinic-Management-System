#pragma once
// Dermatology module: structured assessment, clinical photographs, the three
// decision-support panels (A: clinical knowledge, B: my prescribing patterns,
// C: ClinicFlow network patterns), prescription safety checks, decision tracking,
// follow-up outcomes, doctor analytics and the clinical-content review workflow.
//
// It plugs into ClinicalService as a ConsultationExtension so that safety
// acknowledgement and case capture happen inside the finalize transaction.

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "derm/CdsEngine.hpp"
#include "derm/MedicationSafety.hpp"
#include "security/AuthContext.hpp"
#include "security/Crypto.hpp"
#include "service/ConsultationExtension.hpp"

namespace cf {

class DermService : public ConsultationExtension {
 public:
  using json = nlohmann::json;
  DermService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit, std::shared_ptr<crypto::FieldCipher> cipher)
      : m_db(std::move(db)), m_audit(std::move(audit)), m_cipher(std::move(cipher)) {}

  static json vocabulary();

  // Assessment
  json getAssessment(const AuthContext& ctx, const std::string& consultationId) const;
  json saveAssessment(const AuthContext& ctx, const std::string& consultationId, const json& in);

  // Photographs
  json listPhotos(const AuthContext& ctx, const std::string& consultationId) const;
  json uploadPhoto(const AuthContext& ctx, const std::string& consultationId, const json& in);
  std::pair<std::string, std::string> photoImage(const AuthContext& ctx, const std::string& photoId) const;  // type, bytes
  json markPhotoEnteredInError(const AuthContext& ctx, const std::string& photoId, const json& in);

  // Decision-support panels (each call is stored as a snapshot of what the doctor saw)
  json clinicalPanel(const AuthContext& ctx, const std::string& consultationId);
  json myPatternsPanel(const AuthContext& ctx, const std::string& consultationId);
  json networkPanel(const AuthContext& ctx, const std::string& consultationId);

  // Prescription safety
  json safetyCheck(const AuthContext& ctx, const std::string& consultationId) const;

  // Decisions & outcomes
  json decisions(const AuthContext& ctx, const std::string& consultationId) const;
  json recordOutcome(const AuthContext& ctx, const std::string& consultationId, const json& in);

  // Analytics
  json doctorAnalytics(const AuthContext& ctx) const;
  json networkAnalytics(const AuthContext& ctx) const;

  // Clinical content review
  json contentList(const AuthContext& ctx, const std::string& status) const;
  json reviewContent(const AuthContext& ctx, const std::string& type, const std::string& id, const json& in);
  json updateMedicationClinical(const AuthContext& ctx, const std::string& id, const json& in);

  // Patient choice about network use
  json setPatientNetworkOptOut(const AuthContext& ctx, const std::string& patientId, const json& in);
  // Called when a clinic leaves the network
  void withdrawFacilityFromNetwork(Conn& c, const std::string& facilityId) const;

  // ConsultationExtension
  void beforeFinalize(Conn& c, const AuthContext& ctx, const std::string& consultationId, const FinalizeOptions& opts) override;
  void afterFinalize(Conn& c, const AuthContext& ctx, const std::string& consultationId) override;

  // Keyed, domain-separated pseudonym (not reversible without the server key)
  std::string pseudonym(const std::string& kind, const std::string& id) const {
    return m_cipher->blindIndex("clinicflow-network:" + kind + ":" + id);
  }

 private:
  struct ConsultCtx;
  ConsultCtx loadConsult(Conn& c, const std::string& consultationId) const;
  derm::DermFacts factsFor(Conn& c, const ConsultCtx& cc) const;
  json safetyAlerts(Conn& c, const ConsultCtx& cc) const;
  json annotateForPatient(Conn& c, const ConsultCtx& cc, const std::string& generic) const;
  json catalogueProducts(Conn& c, const std::string& generic) const;
  std::string snapshot(Conn& c, const AuthContext& ctx, const std::string& consultationId, const std::string& source,
                       const json& payload, const std::vector<std::string>& generics) const;

  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
  std::shared_ptr<crypto::FieldCipher> m_cipher;
};

}  // namespace cf
