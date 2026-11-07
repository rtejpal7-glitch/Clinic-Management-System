#include "AbdmService.hpp"

#include <algorithm>
#include <regex>
#include <set>

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "oatpp/core/base/Environment.hpp"

namespace cf {

using text::opt;
using text::trimmed;

static const std::vector<std::string> HI_TYPES = {"OPConsultation", "Prescription", "DiagnosticReport", "DischargeSummary",
                                                  "ImmunizationRecord", "HealthDocumentRecord", "WellnessRecord"};
static const std::vector<std::string> PURPOSES = {"CAREMGT", "BTG", "PUBHLTH", "HPAYMT", "DSRCH", "PATRQT"};

std::string AbdmService::maskAbhaNumber(const std::string& d) {
  if (d.size() != 14) return "XX-XXXX-XXXX-XXXX";
  return "XX-XXXX-XXXX-" + d.substr(10);
}
std::string AbdmService::maskAbhaAddress(const std::string& a) {
  auto at = a.find('@');
  std::string user = at == std::string::npos ? a : a.substr(0, at);
  std::string dom = at == std::string::npos ? "" : a.substr(at);
  std::string shown = user.substr(0, std::min<size_t>(2, user.size()));
  return shown + std::string(std::max<size_t>(3, user.size() > 2 ? user.size() - 2 : 3), '*') + dom;
}
bool AbdmService::isValidAbhaAddress(const std::string& a) {
  // ABHA address: username@domain-suffix (e.g. "@abdm", or "@sbx" in sandbox).
  static const std::regex re("^[a-z0-9][a-z0-9._]{2,31}@[a-z]{2,10}$");
  return std::regex_match(a, re);
}

oatpp::Object<dto::AbdmStatusDto> AbdmService::status() const {
  auto s = dto::AbdmStatusDto::createShared();
  s->mode = m_gateway->mode();
  s->isMock = m_gateway->isMock();
  s->notice = m_gateway->isMock()
                  ? "ABDM integration is running in MOCK mode. No data is sent to or received from ABDM. "
                    "ABHA verifications, consents and shares shown here are simulations."
                  : "Live ABDM gateway";
  s->hfrId = m_db->withConn([](Conn& c) { return c.exec("SELECT hfr_id FROM clinic_settings WHERE id = 1").first().s("hfr_id"); });
  s->supportedHiTypes = oatpp::Vector<oatpp::String>::createShared();
  for (auto& h : HI_TYPES) s->supportedHiTypes->push_back(h);
  s->supportedPurposes = oatpp::Vector<oatpp::String>::createShared();
  for (auto& p : PURPOSES) s->supportedPurposes->push_back(p);
  return s;
}

static oatpp::Object<dto::IdentifierDto> identFromRow(const Row& r) {
  auto i = dto::IdentifierDto::createShared();
  i->id = r.s("id");
  i->idType = r.s("id_type");
  i->valueMasked = r.s("value_masked");
  i->verificationStatus = r.s("verification_status");
  i->verificationMethod = r.s("verification_method");
  i->verifiedAt = r.ts("verified_at");
  i->isMock = r.b("is_mock");
  i->unlinkedAt = r.ts("unlinked_at");
  i->unlinkReason = r.s("unlink_reason");
  i->createdAt = r.ts("created_at");
  return i;
}

oatpp::Vector<oatpp::Object<dto::IdentifierDto>> AbdmService::identifiers(const std::string& patientId) const {
  return m_db->withConn([&](Conn& c) {
    m_patients->requireExists(c, patientId);
    auto out = oatpp::Vector<oatpp::Object<dto::IdentifierDto>>::createShared();
    c.exec("SELECT * FROM patient_identifiers WHERE patient_id = $1 ORDER BY created_at DESC", Params(patientId))
        .each([&](const Row& r) { out->push_back(identFromRow(r)); });
    return out;
  });
}

static oatpp::Object<dto::AbdmTransactionDto> txnFromRow(const Row& r) {
  auto t = dto::AbdmTransactionDto::createShared();
  t->id = r.s("id");
  t->requestId = r.s("request_id");
  t->txnType = r.s("txn_type");
  t->patientId = r.s("patient_id");
  t->consentId = r.s("consent_id");
  t->identifierId = r.s("identifier_id");
  t->status = r.s("status");
  t->attemptCount = r.i32("attempt_count");
  t->maxAttempts = r.i32("max_attempts");
  t->nextRetryAt = r.ts("next_retry_at");
  t->gatewayTxnId = r.s("gateway_txn_id");
  t->errorCode = r.s("error_code");
  t->errorMessage = r.s("error_message");
  t->responseSummary = r.s("response_summary");
  t->isMock = r.b("is_mock");
  t->createdAt = r.ts("created_at");
  t->updatedAt = r.ts("updated_at");
  return t;
}

oatpp::Object<dto::AbdmTransactionDto> AbdmService::fetchTxn(Conn& c, const std::string& id) const {
  if (!text::isUuid(id)) throw ApiError::notFound("ABDM transaction");
  auto r = c.exec("SELECT *, response_summary::text AS response_summary FROM abdm_transactions WHERE id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("ABDM transaction");
  return txnFromRow(r.first());
}

std::string AbdmService::newTxn(Conn& c, const AuthContext* ctx, const std::string& type, const std::optional<std::string>& patientId,
                                const std::optional<std::string>& consentId, const std::optional<std::string>& identifierId,
                                const std::string& relatedEntity, const std::optional<std::string>& relatedId,
                                const nlohmann::json& summary) const {
  return c.exec("INSERT INTO abdm_transactions (txn_type, patient_id, consent_id, identifier_id, related_entity, related_entity_id, "
                "status, request_summary, is_mock, initiated_by) VALUES ($1,$2,$3,$4,$5,$6,'PENDING',$7::jsonb,$8,$9) RETURNING id",
                Params(type, patientId, consentId, identifierId, relatedEntity.empty() ? std::optional<std::string>() : relatedEntity,
                       relatedId, summary.dump(), m_gateway->isMock(),
                       ctx ? std::optional<std::string>(ctx->userId) : std::optional<std::string>()))
      .first()
      .str("id");
}

void AbdmService::applyResult(Conn& c, const std::string& txnId, const GatewayResult& r) const {
  c.exec("UPDATE abdm_transactions SET attempt_count = attempt_count + 1, gateway_txn_id = coalesce($2, gateway_txn_id), "
         "status = CASE WHEN $3 THEN 'SUCCESS' WHEN $4 AND attempt_count + 1 < max_attempts THEN 'RETRY_SCHEDULED' ELSE 'FAILED' END, "
         "next_retry_at = CASE WHEN NOT $3 AND $4 AND attempt_count + 1 < max_attempts "
         "  THEN now() + make_interval(secs => 30 * power(2, attempt_count)::int) END, "
         "error_code = CASE WHEN $3 THEN NULL ELSE $5 END, error_message = CASE WHEN $3 THEN NULL ELSE $6 END, "
         "response_summary = $7::jsonb WHERE id = $1",
         Params(txnId, r.gatewayTxnId.empty() ? std::optional<std::string>() : r.gatewayTxnId, r.ok, r.retryable,
                r.errorCode, r.errorMessage, r.data.dump()));
}

oatpp::Object<dto::AbdmTransactionDto> AbdmService::initAbhaVerification(const AuthContext& ctx, const std::string& patientId,
                                                                         const oatpp::Object<dto::AbhaVerifyInitDto>& in) {
  Validator v;
  std::string idType = text::upper(trimmed(in->idType));
  std::string method = text::upper(trimmed(in->authMethod));
  if (method.empty()) method = "MOBILE_OTP";
  std::string raw = trimmed(in->value);
  std::string normalized, masked;
  v.check(idType == "ABHA_NUMBER" || idType == "ABHA_ADDRESS", "idType", "ABHA_NUMBER or ABHA_ADDRESS");
  v.check(method == "MOBILE_OTP" || method == "AADHAAR_OTP", "authMethod", "MOBILE_OTP or AADHAAR_OTP");
  if (idType == "ABHA_NUMBER") {
    normalized = PatientService::normalizeAbhaNumber(raw);
    bool shapeOk = raw.find_first_not_of("0123456789- ") == std::string::npos && normalized.size() == 14;
    v.check(shapeOk, "value", "ABHA number must have 14 digits (XX-XXXX-XXXX-XXXX)");
    masked = maskAbhaNumber(normalized);
  } else if (idType == "ABHA_ADDRESS") {
    normalized = PatientService::normalizeAbhaAddress(raw);
    v.check(isValidAbhaAddress(normalized), "value", "ABHA address looks like name@abdm");
    masked = maskAbhaAddress(normalized);
  }
  v.check(in->patientConsented && *in->patientConsented, "patientConsented",
          "Record the patient's consent to link their ABHA before proceeding");
  v.throwIfAny();
  std::string hmac = m_cipher->blindIndex(idType + ":" + normalized);

  std::string txnId, identifierId;
  m_db->tx([&](Conn& c) {
    m_patients->requireExists(c, patientId);
    auto other = c.exec("SELECT patient_id FROM patient_identifiers WHERE id_type = $1 AND value_hmac = $2 "
                        "AND verification_status IN ('PENDING','VERIFIED')", Params(idType, hmac));
    if (!other.empty() && other.first().str("patient_id") != patientId)
      throw ApiError::conflict("ABHA_LINKED_ELSEWHERE", "This ABHA identifier is already linked to another patient record");
    if (!other.empty()) {
      auto st = c.exec("SELECT id, verification_status FROM patient_identifiers WHERE id_type = $1 AND value_hmac = $2 "
                       "AND verification_status IN ('PENDING','VERIFIED')", Params(idType, hmac)).first();
      if (st.str("verification_status") == "VERIFIED") throw ApiError::conflict("ALREADY_VERIFIED", "This ABHA identifier is already verified for this patient");
      identifierId = st.str("id");
    } else {
      identifierId = c.exec("INSERT INTO patient_identifiers (patient_id, id_type, value_ciphertext, value_hmac, value_masked, "
                            "verification_status, is_mock, linked_by) VALUES ($1,$2,$3,$4,$5,'PENDING',$6,$7) RETURNING id",
                            Params(patientId, idType, m_cipher->encrypt(normalized), hmac, masked, m_gateway->isMock(), ctx.userId))
                         .first()
                         .str("id");
    }
    txnId = newTxn(c, &ctx, "ABHA_VERIFY_INIT", patientId, std::nullopt, identifierId, "", std::nullopt,
                   {{"idType", idType}, {"valueMasked", masked}, {"authMethod", method}});
    m_audit->record(c, ctx, AuditEvent{"ABHA_LINK_INIT", "patient_identifier", identifierId, patientId, "SUCCESS",
                                       {{"idType", idType}, {"valueMasked", masked}, {"mock", m_gateway->isMock()},
                                        {"patientConsented", true}}});
  });
  // Gateway call happens outside the DB transaction (no network I/O while holding locks).
  auto requestId = m_db->withConn([&](Conn& c) { return c.exec("SELECT request_id FROM abdm_transactions WHERE id = $1", Params(txnId)).first().str("request_id"); });
  GatewayResult r = m_gateway->initAbhaVerification(requestId, idType, normalized, method);
  return m_db->tx([&](Conn& c) {
    applyResult(c, txnId, r);
    if (!r.ok && !r.retryable) c.exec("UPDATE patient_identifiers SET verification_status = 'FAILED' WHERE id = $1", Params(identifierId));
    return fetchTxn(c, txnId);
  });
}

oatpp::Object<dto::AbdmTransactionDto> AbdmService::confirmAbhaVerification(const AuthContext& ctx, const std::string& initTxnId,
                                                                            const std::string& otp) {
  if (!std::regex_match(otp, std::regex("^[0-9]{6}$"))) throw ApiError::badRequest("Enter the 6-digit OTP", {{"otp", "6 digits"}});
  std::string confirmTxn, identifierId, patientId, gwTxn, method;
  m_db->tx([&](Conn& c) {
    auto init = fetchTxn(c, initTxnId);
    if (std::string(*init->txnType) != "ABHA_VERIFY_INIT") throw ApiError::badRequest("Not an ABHA verification transaction");
    if (std::string(*init->status) != "SUCCESS") throw ApiError::conflict("INVALID_STATE", "Verification was not initiated successfully");
    auto fresh = c.exec("SELECT created_at > now() - interval '10 minutes' AS ok, request_summary->>'authMethod' AS m "
                        "FROM abdm_transactions WHERE id = $1", Params(initTxnId)).first();
    if (!fresh.boolv("ok")) throw ApiError::conflict("OTP_EXPIRED", "The OTP session has expired. Start verification again.");
    method = fresh.str("m");
    identifierId = *init->identifierId;
    patientId = *init->patientId;
    gwTxn = init->gatewayTxnId ? std::string(*init->gatewayTxnId) : "";
    auto ident = c.exec("SELECT verification_status FROM patient_identifiers WHERE id = $1", Params(identifierId)).first();
    if (ident.str("verification_status") != "PENDING") throw ApiError::conflict("INVALID_STATE", "Identifier is not awaiting verification");
    auto attempts = c.exec("SELECT count(*) AS n FROM abdm_transactions WHERE identifier_id = $1 AND txn_type = 'ABHA_VERIFY_CONFIRM' "
                           "AND status = 'FAILED' AND created_at > now() - interval '10 minutes'", Params(identifierId)).first().i64v("n");
    if (attempts >= 3) throw ApiError::conflict("TOO_MANY_ATTEMPTS", "Too many incorrect OTP attempts. Start verification again.");
    confirmTxn = newTxn(c, &ctx, "ABHA_VERIFY_CONFIRM", patientId, std::nullopt, identifierId, "", std::nullopt,
                        {{"initTransaction", initTxnId}});
    c.exec("UPDATE abdm_transactions SET max_attempts = 1 WHERE id = $1", Params(confirmTxn));  // OTPs are never retried automatically
  });
  auto requestId = m_db->withConn([&](Conn& c) { return c.exec("SELECT request_id FROM abdm_transactions WHERE id = $1", Params(confirmTxn)).first().str("request_id"); });
  GatewayResult r = m_gateway->confirmAbhaVerification(requestId, gwTxn, otp);
  return m_db->tx([&](Conn& c) {
    applyResult(c, confirmTxn, r);
    if (r.ok) {
      c.exec("UPDATE patient_identifiers SET verification_status = 'VERIFIED', verified_at = now(), verification_method = $2 WHERE id = $1",
             Params(identifierId, method));
    }
    m_audit->record(c, ctx, AuditEvent{"ABHA_LINK_CONFIRM", "patient_identifier", identifierId, patientId, r.ok ? "SUCCESS" : "FAILURE",
                                       {{"mock", m_gateway->isMock()}, {"errorCode", r.errorCode}}});
    return fetchTxn(c, confirmTxn);
  });
}

oatpp::Object<dto::IdentifierDto> AbdmService::unlink(const AuthContext& ctx, const std::string& patientId,
                                                      const std::string& identifierId, const std::string& reason) {
  std::string rs = text::trim(reason);
  if (rs.size() < 3) throw ApiError::badRequest("A reason is required", {{"reason", "Required"}});
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("UPDATE patient_identifiers SET verification_status = 'UNLINKED', unlinked_at = now(), unlinked_by = $3, "
                    "unlink_reason = $4 WHERE id = $1 AND patient_id = $2 AND verification_status <> 'UNLINKED' RETURNING *",
                    Params(identifierId, patientId, ctx.userId, rs));
    if (r.empty()) throw ApiError::notFound("Linked identifier");
    m_audit->record(c, ctx, AuditEvent{"ABHA_UNLINK", "patient_identifier", identifierId, patientId, "SUCCESS", {{"reason", rs}}});
    return identFromRow(r.first());
  });
}

// ---------------- Consent ----------------
static oatpp::Object<dto::ConsentDto> consentFromRow(const Row& r) {
  auto d = dto::ConsentDto::createShared();
  d->id = r.s("id");
  d->patientId = r.s("patient_id");
  d->direction = r.s("direction");
  d->purposeCode = r.s("purpose_code");
  d->hiTypes = r.strArray("hi_types");
  d->dateFrom = r.s("df");
  d->dateTo = r.s("dt");
  d->expiresAt = r.ts("expires_at");
  d->status = r.s("status");
  d->statusReason = r.s("status_reason");
  d->consentRequestRef = r.s("consent_request_ref");
  d->consentArtefactRef = r.s("consent_artefact_ref");
  d->requestedBy = r.s("by_name");
  d->requestedAt = r.ts("requested_at");
  d->respondedAt = r.ts("responded_at");
  d->revokedAt = r.ts("revoked_at");
  d->isMock = r.b("is_mock");
  return d;
}
static const char* CONSENT_SELECT =
    "SELECT pc.*, pc.date_from::text AS df, pc.date_to::text AS dt, u.full_name AS by_name "
    "FROM patient_consent_records pc LEFT JOIN users u ON u.id = pc.requested_by ";

oatpp::Object<dto::ConsentDto> AbdmService::fetchConsent(Conn& c, const std::string& id) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Consent");
  auto r = c.exec(std::string(CONSENT_SELECT) + "WHERE pc.id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Consent");
  return consentFromRow(r.first());
}

oatpp::Vector<oatpp::Object<dto::ConsentDto>> AbdmService::consents(const std::string& patientId) const {
  return m_db->withConn([&](Conn& c) {
    m_patients->requireExists(c, patientId);
    auto out = oatpp::Vector<oatpp::Object<dto::ConsentDto>>::createShared();
    c.exec(std::string(CONSENT_SELECT) + "WHERE pc.patient_id = $1 ORDER BY pc.requested_at DESC", Params(patientId))
        .each([&](const Row& r) { out->push_back(consentFromRow(r)); });
    return out;
  });
}

oatpp::Object<dto::ConsentDto> AbdmService::requestConsent(const AuthContext& ctx, const std::string& patientId,
                                                           const oatpp::Object<dto::ConsentRequestDto>& in) {
  Validator v;
  std::string direction = text::upper(trimmed(in->direction));
  if (direction.empty()) direction = "HIP_SHARE";
  std::string purpose = text::upper(trimmed(in->purposeCode));
  std::string df = trimmed(in->dateFrom), dt = trimmed(in->dateTo), exp = trimmed(in->expiresAt);
  v.check(direction == "HIU_REQUEST" || direction == "HIP_SHARE", "direction", "HIU_REQUEST or HIP_SHARE");
  v.check(std::find(PURPOSES.begin(), PURPOSES.end(), purpose) != PURPOSES.end(), "purposeCode", "Select a valid purpose");
  v.check(in->hiTypes && !in->hiTypes->empty(), "hiTypes", "Select at least one health information type");
  if (in->hiTypes)
    for (auto& h : *in->hiTypes)
      v.check(h && std::find(HI_TYPES.begin(), HI_TYPES.end(), std::string(*h)) != HI_TYPES.end(), "hiTypes", "Unsupported type");
  v.check(text::isIsoDate(df), "dateFrom", "YYYY-MM-DD");
  v.check(text::isIsoDate(dt), "dateTo", "YYYY-MM-DD");
  v.check(!exp.empty(), "expiresAt", "Expiry is required");
  v.throwIfAny();
  if (df > dt) throw ApiError::badRequest("Date range is invalid", {{"dateTo", "Must be on or after the start date"}});

  std::string consentId, txnId;
  m_db->tx([&](Conn& c) {
    m_patients->requireExists(c, patientId);
    auto expOk = c.exec("SELECT ($1::timestamptz > now() AND $1::timestamptz < now() + interval '1 year') AS ok", Params(exp)).first();
    if (!expOk.boolv("ok")) throw ApiError::badRequest("Expiry must be in the future and within one year", {{"expiresAt", "Invalid"}});
    auto abha = c.exec("SELECT 1 FROM patient_identifiers WHERE patient_id = $1 AND verification_status = 'VERIFIED'", Params(patientId));
    if (abha.empty())
      throw ApiError::unprocessable("ABHA_NOT_LINKED", "Link and verify the patient's ABHA before raising an ABDM consent request");
    consentId = c.exec("INSERT INTO patient_consent_records (patient_id, direction, purpose_code, hi_types, date_from, date_to, expires_at, "
                       "status, requested_by, is_mock) VALUES ($1,$2,$3,$4::text[],$5::date,$6::date,$7::timestamptz,'REQUESTED',$8,$9) RETURNING id",
                       Params(patientId, direction, purpose, text::pgArray(in->hiTypes), df, dt, exp, ctx.userId, m_gateway->isMock()))
                    .first()
                    .str("id");
    txnId = newTxn(c, &ctx, "CONSENT_REQUEST", patientId, consentId, std::nullopt, "", std::nullopt,
                   {{"purpose", purpose}, {"hiTypes", text::join(in->hiTypes)}, {"direction", direction}});
    m_audit->record(c, ctx, AuditEvent{"CONSENT_REQUEST", "consent", consentId, patientId, "SUCCESS",
                                       {{"purpose", purpose}, {"hiTypes", text::join(in->hiTypes)}, {"mock", m_gateway->isMock()}}});
  });
  std::vector<std::string> hi;
  for (auto& h : *in->hiTypes) hi.push_back(*h);
  auto requestId = m_db->withConn([&](Conn& c) { return c.exec("SELECT request_id FROM abdm_transactions WHERE id = $1", Params(txnId)).first().str("request_id"); });
  GatewayResult r = m_gateway->requestConsent(requestId, purpose, hi, df, dt, exp);
  return m_db->tx([&](Conn& c) {
    applyResult(c, txnId, r);
    if (r.ok) {
      c.exec("UPDATE patient_consent_records SET consent_request_ref = $2 WHERE id = $1", Params(consentId, r.gatewayTxnId));
    } else if (!r.retryable) {
      c.exec("UPDATE patient_consent_records SET status = 'FAILED', status_reason = $2 WHERE id = $1", Params(consentId, r.errorMessage));
    } else {
      c.exec("UPDATE patient_consent_records SET status_reason = $2 WHERE id = $1",
             Params(consentId, "Gateway unavailable; retry scheduled"));
    }
    return fetchConsent(c, consentId);
  });
}

oatpp::Object<dto::ConsentDto> AbdmService::mockRespond(const AuthContext& ctx, const std::string& consentId, const std::string& decision) {
  if (!m_gateway->isMock()) throw ApiError::forbidden("Simulated responses are only available in mock mode");
  std::string d = text::upper(decision);
  if (d != "GRANT" && d != "DENY") throw ApiError::badRequest("decision must be GRANT or DENY", {{"decision", "GRANT or DENY"}});
  return m_db->tx([&](Conn& c) {
    auto cur = fetchConsent(c, consentId);
    if (std::string(*cur->status) != "REQUESTED") throw ApiError::conflict("INVALID_STATE", "Only pending consent requests can receive a response");
    if (!cur->consentRequestRef) throw ApiError::conflict("INVALID_STATE", "The consent request has not reached the gateway yet");
    std::string artefact = d == "GRANT" ? "MOCK-CA-" + crypto::randomToken(9) : "";
    c.exec("UPDATE patient_consent_records SET status = $2, responded_at = now(), consent_artefact_ref = $3, "
           "status_reason = $4 WHERE id = $1",
           Params(consentId, d == "GRANT" ? "GRANTED" : "DENIED", artefact.empty() ? std::optional<std::string>() : artefact,
                  d == "GRANT" ? "Granted by patient (simulated)" : "Denied by patient (simulated)"));
    std::string t = newTxn(c, &ctx, "CONSENT_NOTIFY", std::string(*cur->patientId), consentId, std::nullopt, "", std::nullopt,
                           {{"decision", d}, {"simulated", true}});
    GatewayResult ok;
    ok.ok = true;
    ok.data = {{"simulated", true}, {"decision", d}};
    applyResult(c, t, ok);
    m_audit->record(c, ctx, AuditEvent{"CONSENT_RESPONSE_SIMULATED", "consent", consentId, std::string(*cur->patientId), "SUCCESS",
                                       {{"decision", d}}});
    return fetchConsent(c, consentId);
  });
}

oatpp::Object<dto::ConsentDto> AbdmService::revoke(const AuthContext& ctx, const std::string& consentId, const std::string& reason) {
  std::string rs = text::trim(reason);
  if (rs.size() < 3) throw ApiError::badRequest("A reason is required", {{"reason", "Required"}});
  return m_db->tx([&](Conn& c) {
    auto cur = fetchConsent(c, consentId);
    std::string st = *cur->status;
    if (st != "GRANTED" && st != "REQUESTED") throw ApiError::conflict("INVALID_STATE", "Only active or pending consents can be revoked");
    c.exec("UPDATE patient_consent_records SET status = 'REVOKED', revoked_at = now(), status_reason = $2 WHERE id = $1", Params(consentId, rs));
    m_audit->record(c, ctx, AuditEvent{"CONSENT_REVOKE", "consent", consentId, std::string(*cur->patientId), "SUCCESS", {{"reason", rs}}});
    return fetchConsent(c, consentId);
  });
}

// ---------------- Health information sharing ----------------
GatewayResult AbdmService::executeShare(const std::string& txnId, std::string& careContextRef, int& resourceCount,
                                        std::vector<std::string>& issues) {
  auto info = m_db->withConn([&](Conn& c) {
    return c.exec("SELECT t.request_id, t.related_entity_id AS cid, pc.consent_artefact_ref, pc.status AS consent_status, "
                  "pc.expires_at > now() AS consent_live, cc.reference FROM abdm_transactions t "
                  "JOIN patient_consent_records pc ON pc.id = t.consent_id "
                  "LEFT JOIN abdm_care_contexts cc ON cc.consultation_id = t.related_entity_id WHERE t.id = $1",
                  Params(txnId)).first();
  });
  GatewayResult r;
  if (info.str("consent_status") != "GRANTED" || !info.boolv("consent_live")) {
    r.errorCode = "CONSENT_NOT_ACTIVE";
    r.errorMessage = "Consent is no longer active (revoked, expired or denied)";
    return r;
  }
  careContextRef = info.str("reference");
  auto bundle = m_fhir->consultationBundle(info.str("cid"));
  issues = FhirMapper::validateBundle(bundle);
  resourceCount = static_cast<int>(bundle["entry"].size());
  if (!issues.empty()) {
    r.errorCode = "FHIR_VALIDATION_FAILED";
    r.errorMessage = "Bundle failed local structural validation (" + std::to_string(issues.size()) + " issues)";
    r.data = {{"issues", issues}};
    return r;
  }
  return m_gateway->pushHealthInformation(info.str("request_id"), info.str("consent_artefact_ref"), careContextRef, bundle);
}

oatpp::Object<dto::ShareResultDto> AbdmService::shareConsultation(const AuthContext& ctx, const std::string& consultationId) {
  std::string txnId, patientId;
  m_db->tx([&](Conn& c) {
    if (!text::isUuid(consultationId)) throw ApiError::notFound("Consultation");
    auto con = c.exec("SELECT patient_id, status, (started_at AT TIME ZONE 'UTC')::date::text AS d FROM consultations WHERE id = $1",
                      Params(consultationId));
    if (con.empty()) throw ApiError::notFound("Consultation");
    if (con.first().str("status") != "FINALIZED")
      throw ApiError::unprocessable("NOT_FINALIZED", "Only finalized consultations can be shared");
    patientId = con.first().str("patient_id");
    if (c.exec("SELECT 1 FROM patient_identifiers WHERE patient_id = $1 AND verification_status = 'VERIFIED'", Params(patientId)).empty())
      throw ApiError::unprocessable("ABHA_NOT_LINKED", "The patient has no verified ABHA link");
    auto consent = c.exec("SELECT id FROM patient_consent_records WHERE patient_id = $1 AND status = 'GRANTED' AND expires_at > now() "
                          "AND direction = 'HIP_SHARE' AND hi_types && ARRAY['OPConsultation','Prescription'] "
                          "AND $2::date BETWEEN date_from AND date_to ORDER BY responded_at DESC LIMIT 1",
                          Params(patientId, con.first().str("d")));
    if (consent.empty())
      throw ApiError::unprocessable("NO_ACTIVE_CONSENT",
                                    "No granted, unexpired consent covers this consultation (purpose HIP_SHARE, OPConsultation/Prescription, date range)");
    std::string consentId = consent.first().str("id");
    c.exec("INSERT INTO abdm_care_contexts (patient_id, consultation_id, reference, display, is_mock) "
           "SELECT $1::uuid, id, 'CC-' || upper(substr(replace(id::text, '-', ''), 1, 12)), "
           "  'OPD consultation ' || to_char(started_at AT TIME ZONE (SELECT timezone FROM clinic_settings WHERE id = 1), 'DD Mon YYYY'), $3 "
           "FROM consultations WHERE id = $2::uuid ON CONFLICT (consultation_id) DO NOTHING",
           Params(patientId, consultationId, m_gateway->isMock()));
    txnId = newTxn(c, &ctx, "HI_SHARE", patientId, consentId, std::nullopt, "consultation", consultationId,
                   {{"hiType", "OPConsultation"}, {"consultationId", consultationId}});
  });
  std::string ccRef;
  int count = 0;
  std::vector<std::string> issues;
  GatewayResult r = executeShare(txnId, ccRef, count, issues);
  return m_db->tx([&](Conn& c) {
    applyResult(c, txnId, r);
    c.exec("UPDATE abdm_care_contexts SET link_status = $2 WHERE consultation_id = $1",
           Params(consultationId, r.ok ? "LINKED" : "NOT_LINKED"));
    m_audit->record(c, ctx, AuditEvent{"HI_SHARE", "consultation", consultationId, patientId, r.ok ? "SUCCESS" : "FAILURE",
                                       {{"transactionId", txnId}, {"mock", m_gateway->isMock()}, {"errorCode", r.errorCode}}});
    auto out = dto::ShareResultDto::createShared();
    out->transaction = fetchTxn(c, txnId);
    out->careContextReference = ccRef;
    out->resourceCount = count;
    out->validationIssues = oatpp::Vector<oatpp::String>::createShared();
    for (auto& i : issues) out->validationIssues->push_back(i);
    return out;
  });
}

oatpp::Vector<oatpp::Object<dto::AbdmTransactionDto>> AbdmService::transactions(const std::optional<std::string>& patientId) const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::AbdmTransactionDto>>::createShared();
    c.exec("SELECT *, response_summary::text AS response_summary FROM abdm_transactions "
           "WHERE ($1::uuid IS NULL OR patient_id = $1::uuid) ORDER BY created_at DESC LIMIT 200",
           Params(patientId))
        .each([&](const Row& r) { out->push_back(txnFromRow(r)); });
    return out;
  });
}

oatpp::Object<dto::AbdmTransactionDto> AbdmService::retry(const AuthContext& ctx, const std::string& txnId) {
  auto t = m_db->withConn([&](Conn& c) { return fetchTxn(c, txnId); });
  std::string type = *t->txnType, st = *t->status;
  if (type != "HI_SHARE" && type != "CONSENT_REQUEST")
    throw ApiError::conflict("NOT_RETRYABLE", "Only consent requests and health-information shares can be retried");
  if (st != "RETRY_SCHEDULED" && st != "FAILED") throw ApiError::conflict("INVALID_STATE", "Transaction is not in a retryable state");
  if (*t->attemptCount >= *t->maxAttempts) throw ApiError::conflict("MAX_ATTEMPTS", "Maximum retry attempts reached");
  GatewayResult r;
  if (type == "HI_SHARE") {
    std::string cc;
    int n;
    std::vector<std::string> issues;
    r = executeShare(txnId, cc, n, issues);
  } else {
    auto info = m_db->withConn([&](Conn& c) {
      return c.exec("SELECT t.request_id, pc.purpose_code, pc.hi_types, pc.date_from::text AS df, pc.date_to::text AS dt, pc.expires_at "
                    "FROM abdm_transactions t JOIN patient_consent_records pc ON pc.id = t.consent_id WHERE t.id = $1", Params(txnId)).first();
    });
    std::vector<std::string> hi;
    auto hiVec = info.strArray("hi_types");  // keep the wrapper alive while iterating
    for (auto& h : *hiVec) hi.push_back(*h);
    r = m_gateway->requestConsent(info.str("request_id"), info.str("purpose_code"), hi, info.str("df"), info.str("dt"), *info.ts("expires_at"));
  }
  return m_db->tx([&](Conn& c) {
    applyResult(c, txnId, r);
    if (type == "CONSENT_REQUEST" && r.ok)
      c.exec("UPDATE patient_consent_records SET consent_request_ref = $2, status_reason = NULL WHERE id = $1",
             Params(std::string(*t->consentId), r.gatewayTxnId));
    if (type == "HI_SHARE" && r.ok)
      c.exec("UPDATE abdm_care_contexts SET link_status = 'LINKED' WHERE consultation_id = (SELECT related_entity_id FROM abdm_transactions WHERE id = $1)",
             Params(txnId));
    m_audit->record(c, ctx, AuditEvent{"ABDM_RETRY", "abdm_transaction", txnId, t->patientId ? std::optional<std::string>(*t->patientId) : std::nullopt,
                                       r.ok ? "SUCCESS" : "FAILURE", {{"type", type}}});
    return fetchTxn(c, txnId);
  });
}

void AbdmService::injectMockFailure(const AuthContext& ctx, const std::string& operation, int count, bool retryable) {
  auto mock = std::dynamic_pointer_cast<MockAbdmGateway>(m_gateway);
  if (!mock) throw ApiError::forbidden("Failure injection is only available in mock mode");
  static const std::set<std::string> ops = {"ABHA_VERIFY_INIT", "ABHA_VERIFY_CONFIRM", "CONSENT_REQUEST", "HI_SHARE"};
  if (!ops.count(operation)) throw ApiError::badRequest("Unknown operation");
  mock->injectFailures(operation, std::clamp(count, 0, 10), retryable);
  m_audit->recordStandalone(AuditActor::from(ctx), AuditEvent{"ABDM_MOCK_FAILURE_INJECTED", "abdm", operation, std::nullopt, "SUCCESS",
                                                              {{"count", count}, {"retryable", retryable}}});
}

nlohmann::json AbdmService::fhirExport(const AuthContext& ctx, const std::string& type, const std::string& id) const {
  nlohmann::json res;
  std::optional<std::string> patientId;
  if (type == "Patient") { res = m_fhir->patient(id); patientId = id; }
  else if (type == "Practitioner") res = m_fhir->practitioner(id);
  else if (type == "Organization") res = m_fhir->organization();
  else if (type == "Appointment") res = m_fhir->appointment(id);
  else if (type == "Encounter") res = m_fhir->encounter(id);
  else if (type == "Invoice") res = m_fhir->invoice(id);
  else if (type == "Bundle") res = m_fhir->consultationBundle(id);
  else throw ApiError::notFound("FHIR resource type");
  if (!patientId) {
    if (res.contains("subject") && res["subject"].contains("reference")) {
      std::string ref = res["subject"]["reference"];
      patientId = ref.substr(ref.find_last_of(":/") + 1);
    } else if (type == "Bundle") {
      for (auto& e : res["entry"])
        if (e["resource"]["resourceType"] == "Patient") patientId = e["resource"]["id"].get<std::string>();
    }
  }
  m_audit->recordStandalone(AuditActor::from(ctx), AuditEvent{"FHIR_EXPORT", type, id, patientId, "SUCCESS", {}});
  return res;
}

void AbdmService::runMaintenance() {
  m_db->tx([&](Conn& c) {
    auto expired = c.exec("UPDATE patient_consent_records SET status = 'EXPIRED', status_reason = 'Consent expired' "
                          "WHERE status IN ('REQUESTED','GRANTED') AND expires_at <= now() RETURNING id, patient_id");
    expired.each([&](const Row& r) {
      m_audit->record(c, AuditActor{std::nullopt, "system", "", {"127.0.0.1", "worker"}},
                      AuditEvent{"CONSENT_EXPIRED", "consent", r.str("id"), r.str("patient_id"), "SUCCESS", {}});
    });
  });
  auto due = m_db->withConn([&](Conn& c) {
    std::vector<std::string> ids;
    c.exec("SELECT id FROM abdm_transactions WHERE status = 'RETRY_SCHEDULED' AND next_retry_at <= now() LIMIT 20")
        .each([&](const Row& r) { ids.push_back(r.str("id")); });
    return ids;
  });
  RequestMeta systemMeta{"127.0.0.1", "worker"};
  for (auto& id : due) {
    try {
      // The worker has no user; record actor as system in the audit trail.
      auto t = m_db->withConn([&](Conn& c) { return fetchTxn(c, id); });
      GatewayResult r;
      std::string type = *t->txnType;
      if (type == "HI_SHARE") {
        std::string cc;
        int n;
        std::vector<std::string> issues;
        r = executeShare(id, cc, n, issues);
      } else {
        continue;  // consent retries are triggered manually from the UI
      }
      m_db->tx([&](Conn& c) {
        applyResult(c, id, r);
        if (r.ok)
          c.exec("UPDATE abdm_care_contexts SET link_status = 'LINKED' WHERE consultation_id = "
                 "(SELECT related_entity_id FROM abdm_transactions WHERE id = $1)", Params(id));
        m_audit->record(c, AuditActor{std::nullopt, "system", "", systemMeta},
                        AuditEvent{"ABDM_AUTO_RETRY", "abdm_transaction", id, std::nullopt, r.ok ? "SUCCESS" : "FAILURE", {{"type", type}}});
      });
    } catch (const std::exception& e) {
      OATPP_LOGE("ABDM", "Retry of %s failed: %s", id.c_str(), e.what());
    }
  }
}

}  // namespace cf
