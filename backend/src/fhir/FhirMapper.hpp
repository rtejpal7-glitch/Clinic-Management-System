#pragma once
// Maps ClinicFlow records to HL7 FHIR R4 resources (JSON).
//
// Profiles: resources declare the ABDM / NRCeS FHIR implementation-guide
// profile canonicals they are *modelled after* (meta.profile). The PoC performs
// local structural validation only (validate*). Conformance to the official
// ABDM profiles must be confirmed with the HL7 FHIR Validator loaded with the
// NRCeS implementation guide before any real exchange — generic FHIR validity
// does not by itself establish ABDM compliance.

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "db/Db.hpp"
#include "security/Crypto.hpp"

namespace cf {

class FhirMapper {
 public:
  FhirMapper(std::shared_ptr<Db> db, std::shared_ptr<crypto::FieldCipher> cipher)
      : m_db(std::move(db)), m_cipher(std::move(cipher)) {}

  nlohmann::json patient(const std::string& patientId) const;
  nlohmann::json practitioner(const std::string& doctorId) const;
  nlohmann::json organization() const;
  nlohmann::json appointment(const std::string& appointmentId) const;
  nlohmann::json encounter(const std::string& consultationId) const;
  nlohmann::json invoice(const std::string& billId) const;
  // ABDM-style OP consultation document bundle (Composition + referenced resources)
  nlohmann::json consultationBundle(const std::string& consultationId) const;

  // Local structural validation (required elements, reference integrity, coding shape).
  static std::vector<std::string> validateResource(const nlohmann::json& r);
  static std::vector<std::string> validateBundle(const nlohmann::json& bundle);

  static constexpr const char* NRCES = "https://nrces.in/ndhm/fhir/r4/StructureDefinition/";

 private:
  nlohmann::json patientFrom(Conn& c, const std::string& id) const;
  nlohmann::json practitionerFrom(Conn& c, const std::string& id) const;
  nlohmann::json organizationFrom(Conn& c) const;
  std::shared_ptr<Db> m_db;
  std::shared_ptr<crypto::FieldCipher> m_cipher;
};

}  // namespace cf
