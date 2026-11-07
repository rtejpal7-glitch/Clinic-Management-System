#pragma once
// Patient registration and master data management (NABH CMS AAC.1 scope).

#include <memory>
#include <nlohmann/json.hpp>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "dto/PatientDtos.hpp"
#include "security/AuthContext.hpp"
#include "security/Crypto.hpp"
#include "service/DuplicateScoring.hpp"

namespace cf {

class PatientService {
 public:
  PatientService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit,
                 std::shared_ptr<crypto::FieldCipher> cipher)
      : m_db(std::move(db)), m_audit(std::move(audit)), m_cipher(std::move(cipher)) {}

  oatpp::Object<dto::DuplicateCheckResultDto> duplicateCheck(const oatpp::Object<dto::PatientInputDto>& in) const;
  oatpp::Object<dto::PatientDto> create(const AuthContext& ctx, const oatpp::Object<dto::PatientInputDto>& in);
  oatpp::Object<dto::PatientDto> get(const AuthContext& ctx, const std::string& id, bool auditView = true) const;
  oatpp::Object<dto::PatientDto> update(const AuthContext& ctx, const std::string& id,
                                        const oatpp::Object<dto::PatientInputDto>& in);
  oatpp::Object<dto::PatientPageDto> search(const AuthContext& ctx, const std::string& q, int page, int size) const;
  oatpp::Vector<oatpp::Object<dto::PatientChangeDto>> history(const AuthContext& ctx, const std::string& id) const;

  oatpp::Vector<oatpp::Object<dto::AllergyDto>> allergies(const std::string& patientId) const;
  oatpp::Object<dto::AllergyDto> addAllergy(const AuthContext& ctx, const std::string& patientId,
                                            const oatpp::Object<dto::AllergyInputDto>& in);
  oatpp::Object<dto::AllergyDto> updateAllergyStatus(const AuthContext& ctx, const std::string& patientId,
                                                     const std::string& allergyId, const std::string& status);

  void requireExists(Conn& c, const std::string& patientId) const;
  static std::string normalizeAbhaNumber(const std::string& v);
  static std::string normalizeAbhaAddress(const std::string& v);

 private:
  struct Fields;
  Fields validate(const oatpp::Object<dto::PatientInputDto>& in) const;
  DuplicateRules loadRules(Conn& c) const;
  std::vector<std::pair<oatpp::Object<dto::PatientDto>, DuplicateScore>> findDuplicates(
      Conn& c, const Fields& f, const std::string& excludeId) const;

  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
  std::shared_ptr<crypto::FieldCipher> m_cipher;
};

oatpp::Object<dto::PatientDto> patientFromRow(const Row& r);
extern const char* PATIENT_SELECT;

}  // namespace cf
