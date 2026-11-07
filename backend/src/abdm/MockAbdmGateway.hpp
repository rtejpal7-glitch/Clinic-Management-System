#pragma once
// MOCK ABDM gateway — for development and demonstration only.
//
// It never contacts ABDM, never generates ABHA numbers, and every record it
// touches is stored with is_mock = true. Deterministic behaviour for testing:
//   * confirm OTP "123456"  -> verification succeeds (simulated)
//   * confirm OTP "999999"  -> simulated gateway timeout (retryable failure)
//   * any other OTP         -> "OTP mismatch" failure
//   * injectFailures(op, n, retryable) makes the next n calls of an operation fail
//     (used by automated tests and the demo "simulate failure" control)

#include <map>
#include <mutex>

#include "AbdmGateway.hpp"

namespace cf {

class MockAbdmGateway : public AbdmGateway {
 public:
  std::string mode() const override { return "mock"; }
  bool isMock() const override { return true; }

  GatewayResult initAbhaVerification(const std::string& requestId, const std::string& idType, const std::string& value,
                                     const std::string& authMethod) override;
  GatewayResult confirmAbhaVerification(const std::string& requestId, const std::string& gatewayTxnId,
                                        const std::string& otp) override;
  GatewayResult requestConsent(const std::string& requestId, const std::string& purpose,
                               const std::vector<std::string>& hiTypes, const std::string& dateFrom,
                               const std::string& dateTo, const std::string& expiry) override;
  GatewayResult pushHealthInformation(const std::string& requestId, const std::string& consentArtefactRef,
                                      const std::string& careContextRef, const nlohmann::json& fhirBundle) override;

  void injectFailures(const std::string& operation, int count, bool retryable);
  static constexpr const char* SUCCESS_OTP = "123456";
  static constexpr const char* TIMEOUT_OTP = "999999";

 private:
  bool consumeInjected(const std::string& op, GatewayResult& out);
  std::mutex m_mutex;
  std::map<std::string, std::pair<int, bool>> m_injected;  // op -> (remaining, retryable)
};

}  // namespace cf
