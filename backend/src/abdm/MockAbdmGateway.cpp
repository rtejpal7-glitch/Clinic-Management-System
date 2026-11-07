#include "MockAbdmGateway.hpp"

#include "security/Crypto.hpp"

namespace cf {

static std::string mockRef(const char* prefix) { return std::string("MOCK-") + prefix + "-" + crypto::randomToken(9); }

void MockAbdmGateway::injectFailures(const std::string& operation, int count, bool retryable) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_injected[operation] = {count, retryable};
}

bool MockAbdmGateway::consumeInjected(const std::string& op, GatewayResult& out) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_injected.find(op);
  if (it == m_injected.end() || it->second.first <= 0) return false;
  it->second.first--;
  out.ok = false;
  out.retryable = it->second.second;
  out.errorCode = it->second.second ? "MOCK_GATEWAY_TIMEOUT" : "MOCK_GATEWAY_REJECTED";
  out.errorMessage = it->second.second ? "Simulated gateway timeout (mock)" : "Simulated rejection by gateway (mock)";
  return true;
}

GatewayResult MockAbdmGateway::initAbhaVerification(const std::string&, const std::string& idType, const std::string&,
                                                    const std::string& authMethod) {
  GatewayResult r;
  if (consumeInjected("ABHA_VERIFY_INIT", r)) return r;
  r.ok = true;
  r.gatewayTxnId = mockRef("TXN");
  r.data = {{"otpSentVia", authMethod}, {"idType", idType}, {"simulated", true},
            {"hint", "Mock mode: use OTP 123456 to simulate success, 999999 to simulate a timeout"}};
  return r;
}

GatewayResult MockAbdmGateway::confirmAbhaVerification(const std::string&, const std::string& gatewayTxnId,
                                                       const std::string& otp) {
  GatewayResult r;
  r.gatewayTxnId = gatewayTxnId;
  if (consumeInjected("ABHA_VERIFY_CONFIRM", r)) return r;
  if (otp == TIMEOUT_OTP) {
    r.retryable = true;
    r.errorCode = "MOCK_GATEWAY_TIMEOUT";
    r.errorMessage = "Simulated gateway timeout (mock). Request a new OTP and try again.";
    return r;
  }
  if (otp != SUCCESS_OTP) {
    r.errorCode = "OTP_MISMATCH";
    r.errorMessage = "The OTP entered is incorrect (mock)";
    return r;
  }
  r.ok = true;
  r.data = {{"verified", true}, {"simulated", true}};
  return r;
}

GatewayResult MockAbdmGateway::requestConsent(const std::string&, const std::string& purpose,
                                              const std::vector<std::string>& hiTypes, const std::string&,
                                              const std::string&, const std::string&) {
  GatewayResult r;
  if (consumeInjected("CONSENT_REQUEST", r)) return r;
  r.ok = true;
  r.gatewayTxnId = mockRef("CR");
  r.data = {{"consentRequestId", r.gatewayTxnId}, {"purpose", purpose}, {"hiTypes", hiTypes}, {"simulated", true},
            {"next", "In mock mode, use 'Simulate patient response' to grant or deny"}};
  return r;
}

GatewayResult MockAbdmGateway::pushHealthInformation(const std::string&, const std::string& consentArtefactRef,
                                                     const std::string& careContextRef, const nlohmann::json& bundle) {
  GatewayResult r;
  if (consumeInjected("HI_SHARE", r)) return r;
  r.ok = true;
  r.gatewayTxnId = mockRef("HI");
  int entries = bundle.contains("entry") ? static_cast<int>(bundle["entry"].size()) : 0;
  r.data = {{"acknowledged", true}, {"simulated", true}, {"careContext", careContextRef}, {"entries", entries},
            {"consentArtefact", consentArtefactRef},
            {"note", "Mock push: bundle was built and validated locally but NOT transmitted to ABDM"}};
  return r;
}

}  // namespace cf
