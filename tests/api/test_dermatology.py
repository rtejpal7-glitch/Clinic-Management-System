"""Dermatology consultation, clinical decision support panels, safety checks,
decision tracking, outcomes and the clinical-content review workflow."""
import base64
import uuid

import pytest

from conftest import (PASSWORD, URTICARIA_ASSESSMENT, create_user, derm_visit, find_med, login, make_doctor,
                      patient_payload, rid, rx_item)

# Smallest valid JPEG header + padding (content is sniffed by magic bytes)
JPEG = b"\xff\xd8\xff\xe0" + b"\x00" * 200 + b"\xff\xd9"


@pytest.fixture(scope="module")
def reviewer(admin):
    return create_user(admin, "CLINICAL_REVIEWER")


@pytest.fixture(scope="module")
def dermdoc(admin):
    return make_doctor(admin)


def content(reviewer):
    r = reviewer.get("/cds/content")
    assert r.status_code == 200, r.text
    return r.json()


def set_condition(reviewer, code, action):
    cond = next(c for c in content(reviewer)["conditions"] if c["code"] == code)
    r = reviewer.post(f"/cds/content/condition/{cond['id']}/review",
                      {"action": action, "notes": "Checked against the cited guideline for automated test"})
    assert r.status_code == 200, r.text


# ---------------------------------------------------------------- assessment
def test_vocabulary(doctor):
    v = doctor.get("/derm/vocabulary").json()
    assert {"WHEAL", "BURROW", "COMEDONE"} <= {x["code"] for x in v["lesionTypes"]}
    assert "FINGER_WEBS" in {x["code"] for x in v["bodySites"]}


def test_assessment_save_validate_and_lock(receptionist, dermdoc):
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    got = dermdoc.get(f"/consultations/{c['id']}/derm").json()
    assert got["exists"] and got["lesionTypes"] == ["WHEAL"] and got["canEdit"] is True
    cons = dermdoc.get(f"/consultations/{c['id']}").json()
    assert cons["specialtyCode"] == "DERMATOLOGY"
    bad = dict(URTICARIA_ASSESSMENT, lesionTypes=["NOT_A_LESION"], severity="EXTREME")
    r = dermdoc.put(f"/consultations/{c['id']}/derm", bad)
    assert r.status_code == 400 and {"lesionTypes", "severity"} <= set(r.json()["error"]["fields"])
    assert dermdoc.post(f"/consultations/{c['id']}/finalize").status_code == 200
    r = dermdoc.put(f"/consultations/{c['id']}/derm", URTICARIA_ASSESSMENT)
    assert r.status_code == 409  # locked after finalization


def test_other_doctor_cannot_edit_or_use_cds(receptionist, dermdoc, other_doctor):
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    assert other_doctor.put(f"/consultations/{c['id']}/derm", URTICARIA_ASSESSMENT).status_code == 403
    assert other_doctor.post(f"/consultations/{c['id']}/cds/clinical").status_code == 403


def test_receptionist_has_no_cds_or_photo_access(receptionist, dermdoc):
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    for path in [f"/consultations/{c['id']}/cds/clinical", f"/consultations/{c['id']}/cds/my-patterns"]:
        assert receptionist.post(path).status_code == 403
    assert receptionist.get(f"/consultations/{c['id']}/photos").status_code == 403


# ---------------------------------------------------------------- photos
def test_photo_consent_type_and_audit(admin, receptionist, dermdoc):
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    b64 = base64.b64encode(JPEG).decode()
    r = dermdoc.post(f"/consultations/{c['id']}/photos", {"dataBase64": b64, "bodySite": "ARMS"})
    assert r.status_code == 400 and "consentObtained" in r.json()["error"]["fields"]
    r = dermdoc.post(f"/consultations/{c['id']}/photos",
                     {"dataBase64": base64.b64encode(b"GIF89a....").decode(), "consentObtained": True})
    assert r.status_code == 400  # not an accepted image type
    r = dermdoc.post(f"/consultations/{c['id']}/photos", {"dataBase64": b64, "bodySite": "ARMS", "caption": "Left forearm",
                                                          "consentObtained": True, "consentScope": "CLINICAL_RECORD_ONLY"})
    assert r.status_code == 201, r.text
    photo = r.json()
    img = dermdoc.get(f"/photos/{photo['id']}/image")
    assert img.status_code == 200 and img.content == JPEG and img.headers["Content-Type"].startswith("image/jpeg")
    assert img.headers.get("Cache-Control") == "no-store"
    assert admin.get(f"/photos/{photo['id']}/image").status_code == 403  # administrators have no clinical access
    logs = admin.get("/audit-logs", params={"entityId": photo["id"]}).json()["items"]
    assert {"PHOTO_CAPTURE", "PHOTO_VIEW"} <= {l["action"] for l in logs}
    r = dermdoc.post(f"/photos/{photo['id']}/entered-in-error", {"reason": "Wrong patient"})
    assert r.status_code == 200 and r.json()["status"] == "ENTERED_IN_ERROR"


# ---------------------------------------------------------------- panel A
def test_panel_a_insufficient_data(receptionist, dermdoc):
    c, _ = derm_visit(receptionist, dermdoc, assessment={"severity": "MILD"}, finalize=False)
    a = dermdoc.post(f"/consultations/{c['id']}/cds/clinical").json()
    assert a["status"] in ("INSUFFICIENT_DATA", "NO_APPROVED_CONTENT")
    assert a["conditions"] == [] and a["treatments"] == []
    if a["status"] == "INSUFFICIENT_DATA":
        assert "Duration" in a["missing"]
    assert "snapshotId" in a and "not a diagnosis" in a["disclaimer"]


def test_unreviewed_content_is_never_used(receptionist, dermdoc, reviewer):
    set_condition(reviewer, "URTICARIA", "REOPEN")
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    a = dermdoc.post(f"/consultations/{c['id']}/cds/clinical").json()
    assert "URTICARIA" not in [x["code"] for x in a["conditions"]]
    set_condition(reviewer, "URTICARIA", "APPROVE")
    a = dermdoc.post(f"/consultations/{c['id']}/cds/clinical").json()
    assert a["status"] == "OK"
    top = a["conditions"][0]
    assert top["code"] == "URTICARIA" and top["strength"] in ("Strong match", "Moderate match")
    assert any("Wheal" in m["finding"] for m in top["matched"])         # reasoning shown
    assert top["references"] and "verified" in top["references"][0]["citation"]
    assert a["questions"] and a["treatments"]
    assert all(t["reference"] for t in a["treatments"])
    # Decision support never edits the prescription by itself
    assert dermdoc.get(f"/consultations/{c['id']}").json()["prescription"]["items"] == []


def test_red_flag_for_breathing_difficulty(receptionist, dermdoc, reviewer):
    flag = next(f for f in content(reviewer)["redFlags"] if f["code"] == "ANGIOEDEMA_AIRWAY")
    reviewer.post(f"/cds/content/red-flag/{flag['id']}/review", {"action": "APPROVE", "notes": "Checked against EAACI 2022 guideline"})
    c, _ = derm_visit(receptionist, dermdoc, finalize=False,
                      assessment=dict(URTICARIA_ASSESSMENT, symptoms=["ITCHING", "BREATHING_DIFFICULTY"]))
    a = dermdoc.post(f"/consultations/{c['id']}/cds/clinical").json()
    assert "ANGIOEDEMA_AIRWAY" in [f["code"] for f in a["redFlags"]]


def test_review_requires_reviewer_and_notes(admin, reviewer, dermdoc):
    cond = content(reviewer)["conditions"][0]
    assert dermdoc.post(f"/cds/content/condition/{cond['id']}/review", {"action": "APPROVE", "notes": "x" * 20}).status_code == 403
    assert admin.post(f"/cds/content/condition/{cond['id']}/review", {"action": "APPROVE", "notes": "x" * 20}).status_code == 403
    assert reviewer.post(f"/cds/content/condition/{cond['id']}/review", {"action": "APPROVE"}).status_code == 400
    # Admins (catalogue.manage) can read the content list, doctors cannot
    assert admin.get("/cds/content").status_code == 200
    assert dermdoc.get("/cds/content").status_code == 403


def test_editing_medicine_information_resets_review(admin, reviewer):
    meds = content(reviewer)["medications"]
    m = next(x for x in meds if x["genericCode"] == "BILASTINE" and not x["brandName"])
    r = reviewer.post(f"/cds/content/medication/{m['id']}/review", {"action": "APPROVE", "notes": "Checked against product label"})
    assert r.status_code == 200
    r = admin.put(f"/catalogue/medications/{m['id']}/clinical", {**{k: m[k] for k in (
        "category", "commonDosage", "warnings", "contraindicationsText", "interactionsText", "reference", "defaultDose",
        "defaultFrequency", "defaultDurationDays", "contraindicationRules", "highRisk", "awareCategory")}, "warnings": "Edited warning"})
    assert r.status_code == 200 and r.json()["reviewStatus"] == "UNREVIEWED"
    reviewer.post(f"/cds/content/medication/{m['id']}/review", {"action": "APPROVE", "notes": "Re-checked against product label"})


# ---------------------------------------------------------------- safety
def test_allergy_blocks_finalize_until_override(receptionist, dermdoc, admin):
    p = receptionist.post("/patients", patient_payload()).json()
    assert dermdoc.post(f"/patients/{p['id']}/allergies", {"substance": "Fexofenadine", "category": "MEDICATION",
                                                           "severity": "MODERATE"}).status_code == 201
    fexo = find_med(dermdoc, "FEXOFENADINE")
    c, _ = derm_visit(receptionist, dermdoc, patient=p, items=[rx_item(fexo)], finalize=False)
    chk = dermdoc.post(f"/consultations/{c['id']}/safety-check").json()
    assert chk["requiresOverride"] and chk["alerts"][0]["code"] == "ALLERGY"
    r = dermdoc.post(f"/consultations/{c['id']}/finalize")
    assert r.status_code == 409 and r.json()["error"]["code"] == "SAFETY_ACKNOWLEDGEMENT_REQUIRED"
    assert r.json()["error"]["data"]["alerts"]
    r = dermdoc.post(f"/consultations/{c['id']}/finalize", {"safetyOverrideReason": "Tolerated previously; label reaction mild"})
    assert r.status_code == 200, r.text
    d = dermdoc.get(f"/consultations/{c['id']}/decisions").json()
    assert d["overrides"] and "Tolerated" in d["overrides"][0]["reason"]
    assert "SAFETY_OVERRIDE" in {l["action"] for l in admin.get("/audit-logs", params={"entityId": c["id"]}).json()["items"]}


def test_pregnancy_and_interaction_alerts(receptionist, dermdoc):
    p = receptionist.post("/patients", patient_payload(gender="FEMALE", dateOfBirth="1998-04-12")).json()
    iso, doxy = find_med(dermdoc, "ISOTRETINOIN"), find_med(dermdoc, "DOXYCYCLINE")
    acne = {"problemType": "ACNE_BREAKOUTS", "durationValue": 6, "durationUnit": "MONTHS", "severity": "SEVERE",
            "lesionTypes": ["NODULE", "COMEDONE"], "bodySites": ["FACE"], "pregnancyStatus": "PREGNANT"}
    c, _ = derm_visit(receptionist, dermdoc, patient=p, assessment=acne, dx=("L70.0", "Acne vulgaris"),
                      items=[rx_item(iso, dose="20 mg", frequency="Once daily", days=30), rx_item(doxy, days=30)], finalize=False)
    alerts = dermdoc.post(f"/consultations/{c['id']}/safety-check").json()["alerts"]
    codes = {a["code"] for a in alerts}
    assert "PATIENT_FACTOR_PREGNANCY" in codes and "INTERACTION_CONTRAINDICATED" in codes and "HIGH_ALERT_MEDICINE" in codes
    # Unreviewed draft catalogue data is shown as a warning, clearly labelled
    for a in alerts:
        if not a["reviewedContent"]:
            assert a["level"] != "CONTRAINDICATED"


def test_free_text_suggestion_source_is_validated(receptionist, dermdoc):
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    fexo = find_med(dermdoc, "FEXOFENADINE")
    r = dermdoc.put(f"/consultations/{c['id']}/prescription",
                    {"items": [rx_item(fexo, sourceKind="NETWORK", sourceSnapshotId=str(uuid.uuid4()))]})
    assert r.status_code == 400


# ---------------------------------------------------------------- panel B + decisions + outcomes
def test_my_patterns_from_real_history_and_decision_tracking(admin, receptionist, dermdoc, other_doctor, reviewer):
    set_condition(reviewer, "URTICARIA", "APPROVE")
    fexo, cet = find_med(dermdoc, "FEXOFENADINE"), find_med(dermdoc, "CETIRIZINE")
    dx = ("L50.9", "Urticaria, unspecified")
    # A fresh doctor has no history -> insufficient
    fresh = make_doctor(admin, "Dr. New")
    c0, _ = derm_visit(receptionist, fresh, finalize=False)
    b0 = fresh.post(f"/consultations/{c0['id']}/cds/my-patterns").json()
    assert b0["status"] == "INSUFFICIENT_DATA" and b0["medications"] == []
    # Three finalized cases for dermdoc
    first, _ = derm_visit(receptionist, dermdoc, items=[rx_item(fexo, dose="120 mg")])
    derm_visit(receptionist, dermdoc, items=[rx_item(fexo, dose="120 mg")])
    derm_visit(receptionist, dermdoc, items=[rx_item(cet, dose="10 mg")])
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    b = dermdoc.post(f"/consultations/{c['id']}/cds/my-patterns").json()
    assert b["status"] == "OK" and b["caseCount"] >= 3
    assert b["diagnosis"]["code"] == "L50.9"
    gens = {m["generic"]: m for m in b["medications"]}
    assert "FEXOFENADINE" in gens and gens["FEXOFENADINE"]["percent"] > 0
    assert "effectiveness" in b["disclaimer"]
    # Another doctor's history is never in my panel
    assert other_doctor.post(f"/consultations/{c['id']}/cds/my-patterns").status_code == 403
    # Doctor explicitly adds the suggestion, modifies the frequency, adds a medicine of their own, finalizes
    item = rx_item(fexo, dose="120 mg", frequency="Twice daily", days=14, sourceKind="MY_PATTERNS",
                   sourceSnapshotId=b["snapshotId"], suggestedFrequency="Once daily", suggestedDose="120 mg",
                   suggestedDurationDays=14)
    emollient = dermdoc.get("/catalogue/medications", params={"q": "calamine"}).json()[0]
    r = dermdoc.put(f"/consultations/{c['id']}/prescription", {"items": [item, rx_item(emollient, dose="Apply", frequency="Three times daily", days=7)]})
    assert r.status_code == 200, r.text
    saved = r.json()["prescription"]["items"][0]
    assert saved["sourceKind"] == "MY_PATTERNS" and saved["genericCode"] == "FEXOFENADINE"
    assert dermdoc.post(f"/consultations/{c['id']}/finalize").status_code == 200
    d = dermdoc.get(f"/consultations/{c['id']}/decisions").json()
    by = {(x["source"], x["generic"]): x["decision"] for x in d["decisions"]}
    assert by[("MY_PATTERNS", "FEXOFENADINE")] == "MODIFIED"
    assert by.get(("DOCTOR", "CALAMINE")) == "DOCTOR_ADDED"
    assert any(x["decision"] == "NOT_USED" for x in d["decisions"] if x["source"] == "MY_PATTERNS") or len(gens) == 1
    # Outcome recording (only on finalized consultations)
    r = dermdoc.post(f"/consultations/{first['id']}/outcome", {"outcome": "IMPROVED", "notes": "Fewer wheals"})
    assert r.status_code == 201
    assert dermdoc.post(f"/consultations/{c0['id']}/outcome", {"outcome": "IMPROVED"}).status_code in (403, 409)
    assert dermdoc.post(f"/consultations/{first['id']}/outcome", {"outcome": "GREAT"}).status_code == 400


# ---------------------------------------------------------------- panel C
def test_network_requires_participation(receptionist, dermdoc, admin):
    admin.put("/facilities/current/network", {"enabled": False})
    c, _ = derm_visit(receptionist, dermdoc, finalize=False)
    r = dermdoc.post(f"/consultations/{c['id']}/cds/network").json()
    assert r["status"] == "NOT_PARTICIPATING" and "medications" not in r or r.get("medications") in (None, [])
    assert admin.put("/facilities/current/network", {"enabled": True, "agreementRef": "DSA-MAIN-TEST"}).status_code == 200


def test_network_patterns_thresholds_and_deidentification(admin, receptionist, dermdoc, clinic_b):
    """Network panel shows nothing until >= 10 cases from >= 3 doctors in >= 2 clinics, and never identities."""
    admin.put("/facilities/current/network", {"enabled": True, "agreementRef": "DSA-MAIN-TEST"})
    clinic_b.put("/facilities/current/network", {"enabled": True, "agreementRef": "DSA-B-TEST"})
    dx = ("L50." + str(uuid.uuid4().int % 10**6), "Urticaria test cohort " + rid(4))  # isolates this run's cohort
    rec_b = create_user(clinic_b, "RECEPTIONIST")
    docs_a = [make_doctor(admin) for _ in range(2)]
    docs_b = [make_doctor(clinic_b) for _ in range(2)]
    fexo_a, fexo_b = find_med(docs_a[0], "FEXOFENADINE"), find_med(docs_b[0], "FEXOFENADINE")
    bil = find_med(docs_a[0], "BILASTINE")
    c, _ = derm_visit(receptionist, dermdoc, dx=dx, finalize=False)
    # Only 2 cases so far -> withheld, without revealing counts
    for d in docs_a:
        derm_visit(receptionist, d, dx=dx, items=[rx_item(fexo_a, dose="120 mg")])
    r = dermdoc.post(f"/consultations/{c['id']}/cds/network").json()
    assert r["status"] == "INSUFFICIENT_DATA" and r["caseCount"] is None and r["medications"] == []
    # Grow to 12 cases, 4 doctors, 2 clinics
    for i in range(4):
        derm_visit(receptionist, docs_a[i % 2], dx=dx, items=[rx_item(bil if i % 2 else fexo_a, dose="20 mg" if i % 2 else "120 mg")])
    for i in range(6):
        derm_visit(rec_b, docs_b[i % 2], dx=dx, items=[rx_item(fexo_b, dose="120 mg")])
    r = dermdoc.post(f"/consultations/{c['id']}/cds/network").json()
    assert r["status"] == "OK", r
    assert r["caseCount"] == 12 and r["doctorCount"] == 4 and r["clinicCount"] == 2
    assert r["period"]["from"] and r["thresholds"]["minCases"] == 10
    meds = {m["generic"]: m for m in r["medications"]}
    assert meds["FEXOFENADINE"]["cases"] == 10 and meds["FEXOFENADINE"]["doctors"] == 4
    text = str(r)
    for d in docs_a + docs_b:
        assert d.doctor_id not in text and d.username not in text
    assert clinic_b.facility["id"] not in text and "facilityId" not in text
    # The current doctor's own cases are excluded from their network view
    own = derm_visit(receptionist, dermdoc, dx=dx, items=[rx_item(fexo_a, dose="120 mg")])
    r2 = dermdoc.post(f"/consultations/{c['id']}/cds/network").json()
    assert r2["caseCount"] == 12
    # Clinic leaves the network -> its records are withdrawn from aggregation
    clinic_b.put("/facilities/current/network", {"enabled": False})
    r3 = dermdoc.post(f"/consultations/{c['id']}/cds/network").json()
    assert r3["status"] == "INSUFFICIENT_DATA"


def test_patient_opt_out_excludes_from_network(receptionist, dermdoc, admin):
    admin.put("/facilities/current/network", {"enabled": True, "agreementRef": "DSA-MAIN-TEST"})
    p = receptionist.post("/patients", patient_payload()).json()
    r = receptionist.put(f"/patients/{p['id']}/network-opt-out", {"optOut": True})
    assert r.status_code == 200 and r.json()["networkOptOut"] is True
    fexo = find_med(dermdoc, "FEXOFENADINE")
    c, _ = derm_visit(receptionist, dermdoc, patient=p, items=[rx_item(fexo, dose="120 mg")])
    logs = admin.get("/audit-logs", params={"entityId": c["id"], "action": "PRESCRIBING_CASE_CAPTURE"}).json()["items"]
    import json as _j
    assert logs and _j.loads(logs[0]["details"])["networkShared"] is False


# ---------------------------------------------------------------- analytics
def test_doctor_dashboard_analytics(dermdoc, admin, receptionist):
    a = dermdoc.get("/analytics/doctor")
    assert a.status_code == 200, a.text
    body = a.json()
    assert {"today", "todayAppointments", "followUpsDue", "recentPrescriptions", "topMedicines90d", "monthly", "outcomes"} <= set(body)
    assert len(body["monthly"]) == 6
    assert admin.get("/analytics/doctor").status_code == 403
    assert receptionist.get("/analytics/network").status_code == 403


def test_network_analytics_hides_small_cohorts(dermdoc):
    r = dermdoc.get("/analytics/network").json()
    assert r["status"] in ("OK", "NOT_PARTICIPATING")
    for d in r.get("diagnoses", []):
        assert d["cases"] >= 10 and d["doctors"] >= 3 and d["clinics"] >= 2
