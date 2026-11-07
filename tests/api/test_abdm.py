"""ABDM readiness: mock ABHA linking, consent lifecycle, mock transactions, FHIR."""
import datetime as dt
import random

import pytest

from conftest import IST


def abha_number():
    # Synthetic 14-digit test value used only against the MOCK adapter.
    return "99-" + "-".join("".join(random.choices("0123456789", k=4)) for _ in range(3))


def link_abha(c, patient_id):
    init = c.post(f"/patients/{patient_id}/abha/verify-init",
                  {"idType": "ABHA_NUMBER", "value": abha_number(), "authMethod": "MOBILE_OTP", "patientConsented": True})
    assert init.status_code == 200, init.text
    assert init.json()["status"] == "SUCCESS" and init.json()["isMock"] is True
    conf = c.post(f"/abdm/transactions/{init.json()['id']}/verify-confirm", {"otp": "123456"})
    assert conf.status_code == 200 and conf.json()["status"] == "SUCCESS"
    return init.json()


def grant_consent(c, patient_id):
    today = dt.datetime.now(IST).date()
    r = c.post(f"/patients/{patient_id}/consents", {
        "direction": "HIP_SHARE", "purposeCode": "CAREMGT", "hiTypes": ["OPConsultation", "Prescription"],
        "dateFrom": (today - dt.timedelta(days=30)).isoformat(), "dateTo": (today + dt.timedelta(days=1)).isoformat(),
        "expiresAt": (dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=30)).isoformat()})
    assert r.status_code == 201, r.text
    assert r.json()["status"] == "REQUESTED" and r.json()["consentRequestRef"].startswith("MOCK-")
    g = c.post(f"/consents/{r.json()['id']}/mock-respond", {"decision": "GRANT"})
    assert g.status_code == 200 and g.json()["status"] == "GRANTED"
    return g.json()


def test_status_reports_mock_mode(receptionist):
    s = receptionist.get("/abdm/status").json()
    assert s["mode"] == "mock" and s["isMock"] is True and "MOCK" in s["notice"]


def test_mock_abha_linking_flow(receptionist, new_patient):
    p = new_patient()
    init = link_abha(receptionist, p["id"])
    ids = receptionist.get(f"/patients/{p['id']}/abha").json()
    assert ids[0]["verificationStatus"] == "VERIFIED" and ids[0]["isMock"] is True
    assert ids[0]["valueMasked"].startswith("XX-XXXX-XXXX-")
    assert receptionist.get(f"/patients/{p['id']}").json()["abhaLinked"] is True
    # internal patient id is unchanged and distinct from ABHA
    assert receptionist.get(f"/patients/{p['id']}").json()["patientNumber"] == p["patientNumber"]


def test_abha_linking_requires_patient_consent_and_valid_format(receptionist, new_patient):
    p = new_patient()
    r = receptionist.post(f"/patients/{p['id']}/abha/verify-init", {"idType": "ABHA_NUMBER", "value": abha_number()})
    assert r.status_code == 400 and "patientConsented" in r.json()["error"]["fields"]
    r = receptionist.post(f"/patients/{p['id']}/abha/verify-init", {"idType": "ABHA_NUMBER", "value": "12345", "patientConsented": True})
    assert r.status_code == 400 and "value" in r.json()["error"]["fields"]


def test_wrong_otp_and_timeout_fail(receptionist, new_patient):
    p = new_patient()
    init = receptionist.post(f"/patients/{p['id']}/abha/verify-init",
                             {"idType": "ABHA_ADDRESS", "value": f"test.{random.randint(1000, 9999)}@sbx", "patientConsented": True}).json()
    bad = receptionist.post(f"/abdm/transactions/{init['id']}/verify-confirm", {"otp": "111111"}).json()
    assert bad["status"] == "FAILED" and bad["errorCode"] == "OTP_MISMATCH"
    to = receptionist.post(f"/abdm/transactions/{init['id']}/verify-confirm", {"otp": "999999"}).json()
    assert to["status"] == "FAILED" and to["errorCode"] == "MOCK_GATEWAY_TIMEOUT"  # OTPs never auto-retry
    assert receptionist.get(f"/patients/{p['id']}/abha").json()[0]["verificationStatus"] == "PENDING"


def test_same_abha_cannot_link_two_patients(receptionist, new_patient):
    value = abha_number()
    p1, p2 = new_patient(), new_patient()
    receptionist.post(f"/patients/{p1['id']}/abha/verify-init", {"idType": "ABHA_NUMBER", "value": value, "patientConsented": True})
    r = receptionist.post(f"/patients/{p2['id']}/abha/verify-init", {"idType": "ABHA_NUMBER", "value": value, "patientConsented": True})
    assert r.status_code == 409 and r.json()["error"]["code"] == "ABHA_LINKED_ELSEWHERE"


def test_search_by_abha_number(receptionist, new_patient):
    p = new_patient()
    value = abha_number()
    init = receptionist.post(f"/patients/{p['id']}/abha/verify-init", {"idType": "ABHA_NUMBER", "value": value, "patientConsented": True}).json()
    receptionist.post(f"/abdm/transactions/{init['id']}/verify-confirm", {"otp": "123456"})
    res = receptionist.get("/patients", params={"q": value}).json()
    assert res["total"] == 1 and res["items"][0]["id"] == p["id"]


def test_consent_requires_verified_abha(receptionist, new_patient):
    p = new_patient()
    r = receptionist.post(f"/patients/{p['id']}/consents", {"purposeCode": "CAREMGT", "hiTypes": ["OPConsultation"],
                                                            "dateFrom": "2026-01-01", "dateTo": "2026-12-31",
                                                            "expiresAt": "2027-01-01T00:00:00Z"})
    assert r.status_code == 422 and r.json()["error"]["code"] == "ABHA_NOT_LINKED"


def test_consent_creation_denial_and_withdrawal(receptionist, new_patient):
    p = new_patient()
    link_abha(receptionist, p["id"])
    g = grant_consent(receptionist, p["id"])
    assert g["consentArtefactRef"].startswith("MOCK-CA-")
    rv = receptionist.post(f"/consents/{g['id']}/revoke", {"reason": "Patient withdrew consent at front desk"})
    assert rv.status_code == 200 and rv.json()["status"] == "REVOKED"
    assert receptionist.post(f"/consents/{g['id']}/revoke", {"reason": "again"}).status_code == 409
    # denied flow
    today = dt.date.today()
    c = receptionist.post(f"/patients/{p['id']}/consents", {"purposeCode": "CAREMGT", "hiTypes": ["Prescription"],
                                                            "dateFrom": today.isoformat(), "dateTo": today.isoformat(),
                                                            "expiresAt": (dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=2)).isoformat()}).json()
    d = receptionist.post(f"/consents/{c['id']}/mock-respond", {"decision": "DENY"}).json()
    assert d["status"] == "DENIED" and d["consentArtefactRef"] is None if "consentArtefactRef" in d else d["status"] == "DENIED"


def test_consent_request_gateway_failure_and_retry(receptionist, new_patient):
    p = new_patient()
    link_abha(receptionist, p["id"])
    receptionist.post("/abdm/mock/inject-failure", {"operation": "CONSENT_REQUEST", "count": 1, "retryable": True})
    today = dt.date.today()
    c = receptionist.post(f"/patients/{p['id']}/consents", {"purposeCode": "CAREMGT", "hiTypes": ["OPConsultation"],
                                                            "dateFrom": today.isoformat(), "dateTo": today.isoformat(),
                                                            "expiresAt": (dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=2)).isoformat()}).json()
    assert c["status"] == "REQUESTED" and "consentRequestRef" not in c
    txn = next(t for t in receptionist.get("/abdm/transactions", params={"patientId": p["id"]}).json() if t["txnType"] == "CONSENT_REQUEST")
    assert txn["status"] == "RETRY_SCHEDULED" and txn["errorCode"] == "MOCK_GATEWAY_TIMEOUT"
    rt = receptionist.post(f"/abdm/transactions/{txn['id']}/retry").json()
    assert rt["status"] == "SUCCESS" and rt["attemptCount"] == 2


@pytest.fixture
def finalized(receptionist, doctor, new_patient):
    p = new_patient()
    a = receptionist.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": doctor.doctor_id}).json()
    c = doctor.post(f"/appointments/{a['id']}/start-consultation").json()
    doctor.post(f"/patients/{p['id']}/allergies", {"substance": "Sulfa drugs", "category": "MEDICATION", "severity": "MODERATE"})
    c = doctor.put(f"/consultations/{c['id']}", {
        "version": c["version"], "chiefComplaint": "Headache", "examinationNotes": "NAD",
        "vitals": {"bpSystolic": 150, "bpDiastolic": 95, "pulse": 80, "temperatureC": 37.0, "spo2": 99, "heightCm": 165, "weightKg": 70},
        "diagnoses": [{"codeSystem": "ICD-10", "code": "I10", "display": "Essential (primary) hypertension"}]}).json()
    med = doctor.get("/catalogue/medications", params={"q": "amlodipine"}).json()[0]
    lab = doctor.get("/catalogue/investigations", params={"q": "lipid"}).json()[0]
    doctor.put(f"/consultations/{c['id']}/prescription", {
        "advice": "Low salt diet", "items": [{"medicationId": med["id"], "medicineName": med["genericName"], "strength": med["strength"],
                                              "dose": "1 tablet", "route": "ORAL", "frequency": "1-0-0", "durationDays": 30}],
        "investigations": [{"investigationId": lab["id"]}]})
    doctor.post(f"/consultations/{c['id']}/finalize")
    return {"patient": p, "consultation": c}


def test_fhir_bundle_structure_and_validation(doctor, finalized):
    cid = finalized["consultation"]["id"]
    b = doctor.get(f"/fhir/Bundle/{cid}").json()
    assert b["resourceType"] == "Bundle" and b["type"] == "document"
    types = [e["resource"]["resourceType"] for e in b["entry"]]
    assert types[0] == "Composition"
    assert {"Patient", "Practitioner", "Organization", "Encounter", "Condition", "Observation", "MedicationRequest",
            "ServiceRequest", "AllergyIntolerance"} <= set(types)
    comp = b["entry"][0]["resource"]
    assert comp["meta"]["profile"][0].endswith("/OPConsultRecord")
    assert comp["type"]["coding"][0]["code"] == "371530004"
    cond = next(e["resource"] for e in b["entry"] if e["resource"]["resourceType"] == "Condition")
    assert cond["code"]["coding"][0] == {"system": "http://hl7.org/fhir/sid/icd-10", "code": "I10",
                                         "display": "Essential (primary) hypertension"}
    bp = next(e["resource"] for e in b["entry"] if e["resource"]["resourceType"] == "Observation"
              and e["resource"]["code"]["coding"][0]["code"] == "85354-9")
    assert {c["code"]["coding"][0]["code"] for c in bp["component"]} == {"8480-6", "8462-4"}
    sr = next(e["resource"] for e in b["entry"] if e["resource"]["resourceType"] == "ServiceRequest")
    assert sr["code"]["coding"][0]["system"] == "http://loinc.org"
    v = doctor.get(f"/fhir/Bundle/{cid}/validate").json()
    assert v["valid"] is True, v["issues"]


def test_fhir_single_resources(doctor, finalized):
    pid = finalized["patient"]["id"]
    pat = doctor.get(f"/fhir/Patient/{pid}").json()
    assert pat["resourceType"] == "Patient" and pat["identifier"][0]["value"] == finalized["patient"]["patientNumber"]
    enc = doctor.get(f"/fhir/Encounter/{finalized['consultation']['id']}").json()
    assert enc["status"] == "finished" and enc["class"]["code"] == "AMB"
    assert doctor.get("/fhir/Unknown/abc").status_code == 404


def test_share_requires_abha_and_granted_consent(doctor, receptionist, finalized):
    cid, pid = finalized["consultation"]["id"], finalized["patient"]["id"]
    r = doctor.post(f"/consultations/{cid}/abdm-share")
    assert r.status_code == 422 and r.json()["error"]["code"] == "ABHA_NOT_LINKED"
    link_abha(receptionist, pid)
    r = doctor.post(f"/consultations/{cid}/abdm-share")
    assert r.status_code == 422 and r.json()["error"]["code"] == "NO_ACTIVE_CONSENT"


def test_mock_share_success_and_failure_with_retry(doctor, receptionist, finalized):
    cid, pid = finalized["consultation"]["id"], finalized["patient"]["id"]
    link_abha(receptionist, pid)
    grant_consent(receptionist, pid)
    ok = doctor.post(f"/consultations/{cid}/abdm-share")
    assert ok.status_code == 200, ok.text
    res = ok.json()
    assert res["transaction"]["status"] == "SUCCESS" and res["transaction"]["isMock"] is True
    assert res["careContextReference"].startswith("CC-") and res["resourceCount"] >= 9
    assert res["validationIssues"] == []
    assert "NOT transmitted" in res["transaction"]["responseSummary"]
    # simulated transient failure, then manual retry succeeds
    doctor.post("/abdm/mock/inject-failure", {"operation": "HI_SHARE", "count": 1, "retryable": True})
    fail = doctor.post(f"/consultations/{cid}/abdm-share").json()
    assert fail["transaction"]["status"] == "RETRY_SCHEDULED"
    rt = doctor.post(f"/abdm/transactions/{fail['transaction']['id']}/retry").json()
    assert rt["status"] == "SUCCESS"


def test_share_blocked_after_consent_revoked(doctor, receptionist, finalized):
    cid, pid = finalized["consultation"]["id"], finalized["patient"]["id"]
    link_abha(receptionist, pid)
    g = grant_consent(receptionist, pid)
    receptionist.post(f"/consents/{g['id']}/revoke", {"reason": "Withdrawn"})
    r = doctor.post(f"/consultations/{cid}/abdm-share")
    assert r.status_code == 422 and r.json()["error"]["code"] == "NO_ACTIVE_CONSENT"


def test_abha_values_encrypted_at_rest(receptionist, new_patient):
    """The API never returns the raw ABHA; storage is verified in docs/TESTING.md via psql."""
    p = new_patient()
    link_abha(receptionist, p["id"])
    ident = receptionist.get(f"/patients/{p['id']}/abha").json()[0]
    assert "value" not in ident and ident["valueMasked"].startswith("XX-")
