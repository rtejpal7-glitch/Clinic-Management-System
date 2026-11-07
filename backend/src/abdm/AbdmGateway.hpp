#pragma once
// Port (interface) between ClinicFlow and the ABDM ecosystem.
//
// Business code depends only on this interface. Two implementations are planned:
//   * MockAbdmGateway  — shipped; deterministic simulation for development/demo.
//   * Live adapter     — NOT implemented. Requires ABDM onboarding (HFR facility
//                        registration, HIP/HIU bridge registration), sandbox and
//                        then production credentials, gateway session tokens,
//                        callback endpoints, and the request/response formats of
//                        the ABDM specifications in force at the time.
//
// The method set mirrors the shape of ABDM flows (initiate → callback/confirm,
// consent request → notification, data push against a consent artefact) but the
// signatures are ClinicFlow's own abstraction, not the ABDM wire protocol.

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cf {

struct GatewayResult {
  bool ok = false;
  bool retryable = false;     // transient failure (timeout, 5xx) — safe to retry with same request id
  std::string gatewayTxnId;   // correlation id returned by the gateway
  std::string errorCode;
  std::string errorMessage;
  nlohmann::json data = nlohmann::json::object();  // non-sensitive response summary
};

class AbdmGateway {
 public:
  virtual ~AbdmGateway() = default;
  virtual std::string mode() const = 0;   // "mock" | "sandbox" | "production"
  virtual bool isMock() const = 0;

  // ABHA verification for linking: initiate (gateway sends OTP to the patient) then confirm.
  virtual GatewayResult initAbhaVerification(const std::string& requestId, const std::string& idType,
                                             const std::string& value, const std::string& authMethod) = 0;
  virtual GatewayResult confirmAbhaVerification(const std::string& requestId, const std::string& gatewayTxnId,
                                                const std::string& otp) = 0;

  // Consent request raised on the patient's behalf; the patient approves in their PHR app.
  virtual GatewayResult requestConsent(const std::string& requestId, const std::string& purpose,
                                       const std::vector<std::string>& hiTypes, const std::string& dateFrom,
                                       const std::string& dateTo, const std::string& expiry) = 0;

  // Push a FHIR document bundle to the requester against a granted consent artefact.
  virtual GatewayResult pushHealthInformation(const std::string& requestId, const std::string& consentArtefactRef,
                                              const std::string& careContextRef, const nlohmann::json& fhirBundle) = 0;
};

}  // namespace cf
