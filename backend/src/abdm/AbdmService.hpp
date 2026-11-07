#pragma once
// ABDM readiness workflows, isolated from patient/clinical modules:
//  - ABHA linking (initiate -> OTP confirm) with encrypted identifier storage
//  - Consent requests, status tracking, expiry, revocation
//  - Health-information sharing of finalized consultations as FHIR bundles
//  - Transaction log with idempotent request ids, error capture and retries
// Talks to ABDM only through the AbdmGateway port (mock in this PoC).

#include <memory>

#include "abdm/AbdmGateway.hpp"
#include "abdm/MockAbdmGateway.hpp"
#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "dto/AbdmDtos.hpp"
#include "fhir/FhirMapper.hpp"
#include "security/AuthContext.hpp"
#include "security/Crypto.hpp"
#include "service/ClinicalService.hpp"
#include "service/PatientService.hpp"

namespace cf {

class AbdmService {
 public:
  AbdmService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit, std::shared_ptr<crypto::FieldCipher> cipher,
              std::shared_ptr<AbdmGateway> gateway, std::shared_ptr<ClinicalService> clinical,
              std::shared_ptr<PatientService> patients)
      : m_db(db), m_audit(std::move(audit)), m_cipher(cipher), m_gateway(std::move(gateway)),
        m_clinical(std::move(clinical)), m_patients(std::move(patients)), m_fhir(std::make_shared<FhirMapper>(db, cipher)) {}

  oatpp::Object<dto::AbdmStatusDto> status() const;

  oatpp::Vector<oatpp::Object<dto::IdentifierDto>> identifiers(const std::string& patientId) const;
  oatpp::Object<dto::AbdmTransactionDto> initAbhaVerification(const AuthContext& ctx, const std::string& patientId,
                                                              const oatpp::Object<dto::AbhaVerifyInitDto>& in);
  oatpp::Object<dto::AbdmTransactionDto> confirmAbhaVerification(const AuthContext& ctx, const std::string& txnId,
                                                                 const std::string& otp);
  oatpp::Object<dto::IdentifierDto> unlink(const AuthContext& ctx, const std::string& patientId,
                                           const std::string& identifierId, const std::string& reason);

  oatpp::Vector<oatpp::Object<dto::ConsentDto>> consents(const std::string& patientId) const;
  oatpp::Object<dto::ConsentDto> requestConsent(const AuthContext& ctx, const std::string& patientId,
                                                const oatpp::Object<dto::ConsentRequestDto>& in);
  oatpp::Object<dto::ConsentDto> mockRespond(const AuthContext& ctx, const std::string& consentId, const std::string& decision);
  oatpp::Object<dto::ConsentDto> revoke(const AuthContext& ctx, const std::string& consentId, const std::string& reason);

  oatpp::Object<dto::ShareResultDto> shareConsultation(const AuthContext& ctx, const std::string& consultationId);
  oatpp::Vector<oatpp::Object<dto::AbdmTransactionDto>> transactions(const std::optional<std::string>& patientId) const;
  oatpp::Object<dto::AbdmTransactionDto> retry(const AuthContext& ctx, const std::string& txnId);
  void injectMockFailure(const AuthContext& ctx, const std::string& operation, int count, bool retryable);

  // FHIR export (audited)
  nlohmann::json fhirExport(const AuthContext& ctx, const std::string& type, const std::string& id) const;

  void runMaintenance();  // consent expiry + scheduled retries

  static std::string maskAbhaNumber(const std::string& digits);
  static std::string maskAbhaAddress(const std::string& addr);
  static bool isValidAbhaAddress(const std::string& addr);

 private:
  oatpp::Object<dto::AbdmTransactionDto> fetchTxn(Conn& c, const std::string& id) const;
  oatpp::Object<dto::ConsentDto> fetchConsent(Conn& c, const std::string& id) const;
  std::string newTxn(Conn& c, const AuthContext* ctx, const std::string& type, const std::optional<std::string>& patientId,
                     const std::optional<std::string>& consentId, const std::optional<std::string>& identifierId,
                     const std::string& relatedEntity, const std::optional<std::string>& relatedId,
                     const nlohmann::json& summary) const;
  void applyResult(Conn& c, const std::string& txnId, const GatewayResult& r) const;
  GatewayResult executeShare(const std::string& txnId, std::string& careContextRef, int& resourceCount,
                             std::vector<std::string>& issues);

  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
  std::shared_ptr<crypto::FieldCipher> m_cipher;
  std::shared_ptr<AbdmGateway> m_gateway;
  std::shared_ptr<ClinicalService> m_clinical;
  std::shared_ptr<PatientService> m_patients;
  std::shared_ptr<FhirMapper> m_fhir;
};

}  // namespace cf
