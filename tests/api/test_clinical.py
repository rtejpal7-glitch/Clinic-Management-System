"""Consultation (EMR), prescriptions, CPOE orders, finalization and locking."""
import pytest


@pytest.fixture
def started(receptionist, doctor, new_patient):
    """A walk-in patient whose consultation has been started by the doctor."""
    p = new_patient()
    a = receptionist.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": doctor.doctor_id}).json()
    r = doctor.post(f"/appointments/{a['id']}/start-consultation")
    assert r.status_code == 200, r.text
    return {"patient": p, "appointment": a, "consultation": r.json()}


def full_note(version):
    return {"version": version, "chiefComplaint": "Fever and sore throat for 3 days", "historyPresentIllness": "Intermittent fever",
            "examinationNotes": "Throat congested", "treatmentPlan": "Symptomatic care", "followUpInstructions": "Review if fever persists",
            "vitals": {"bpSystolic": 118, "bpDiastolic": 76, "pulse": 88, "temperatureC": 38.4, "respiratoryRate": 18, "spo2": 98,
                       "heightCm": 170, "weightKg": 68},
            "diagnoses": [{"codeSystem": "ICD-10", "code": "J02.9", "display": "Acute pharyngitis, unspecified"}]}


def test_consultation_creation_and_draft_save(started, doctor):
    c = started["consultation"]
    assert c["status"] == "DRAFT" and c["canEdit"] is True
    r = doctor.put(f"/consultations/{c['id']}", full_note(c["version"]))
    assert r.status_code == 200, r.text
    body = r.json()
    assert body["vitals"]["bmi"] == 23.5
    assert body["diagnoses"][0]["diagnosisType"] == "PRIMARY"
    appt = doctor.get(f"/appointments/{started['appointment']['id']}").json()
    assert appt["status"] == "IN_CONSULTATION"


def test_start_is_idempotent(started, doctor):
    again = doctor.post(f"/appointments/{started['appointment']['id']}/start-consultation").json()
    assert again["id"] == started["consultation"]["id"]


def test_vitals_validation(started, doctor):
    c = started["consultation"]
    bad = full_note(c["version"])
    bad["vitals"] = {"bpSystolic": 70, "bpDiastolic": 90, "spo2": 120}
    r = doctor.put(f"/consultations/{c['id']}", bad)
    assert r.status_code == 400
    assert "vitals.spo2" in r.json()["error"]["fields"] and "vitals.bpDiastolic" in r.json()["error"]["fields"]


def test_prescription_and_investigation_orders(started, doctor):
    c = started["consultation"]
    med = doctor.get("/catalogue/medications", params={"q": "paracetamol"}).json()[0]
    inv = doctor.get("/catalogue/investigations", params={"q": "blood count"}).json()[0]
    rx = {"advice": "Warm saline gargles. Plenty of fluids.",
          "items": [{"medicationId": med["id"], "medicineName": med["genericName"], "strength": med["strength"],
                     "dosageForm": med["dosageForm"], "dose": "1 tablet", "route": "ORAL", "frequency": "1-1-1",
                     "durationDays": 3, "quantity": 9, "instructions": "After food"}],
          "investigations": [{"investigationId": inv["id"], "priority": "ROUTINE"},
                             {"name": "Throat swab culture", "category": "LAB", "priority": "URGENT"}]}
    r = doctor.put(f"/consultations/{c['id']}/prescription", rx)
    assert r.status_code == 200, r.text
    p = r.json()["prescription"]
    assert p["items"][0]["medicineName"] == "Paracetamol"
    assert {o["name"] for o in p["investigations"]} == {inv["name"], "Throat swab culture"}
    assert next(o for o in p["investigations"] if o["name"] == inv["name"])["loincCode"] == inv["loincCode"]


def test_prescription_validation(started, doctor):
    c = started["consultation"]
    r = doctor.put(f"/consultations/{c['id']}/prescription", {"items": [{"medicineName": "X"}]})
    assert r.status_code == 400
    assert {"items[0].dose", "items[0].frequency", "items[0].route"} <= set(r.json()["error"]["fields"])


def test_finalize_requires_complaint_and_diagnosis(started, doctor):
    r = doctor.post(f"/consultations/{started['consultation']['id']}/finalize")
    assert r.status_code == 400
    assert {"chiefComplaint", "diagnoses"} <= set(r.json()["error"]["fields"])


def test_finalized_record_is_locked_and_addenda_work(started, doctor, receptionist):
    c = started["consultation"]
    saved = doctor.put(f"/consultations/{c['id']}", full_note(c["version"])).json()
    fin = doctor.post(f"/consultations/{c['id']}/finalize")
    assert fin.status_code == 200, fin.text
    assert fin.json()["status"] == "FINALIZED" and fin.json()["prescription"]["status"] == "FINALIZED"
    # appointment completed, token done
    assert receptionist.get(f"/appointments/{started['appointment']['id']}").json()["status"] == "COMPLETED"
    # no further edits
    r = doctor.put(f"/consultations/{c['id']}", full_note(saved["version"] + 1))
    assert r.status_code == 409 and r.json()["error"]["code"] == "RECORD_LOCKED"
    assert doctor.put(f"/consultations/{c['id']}/prescription", {"advice": "changed"}).status_code == 409
    # corrections go through addenda
    a = doctor.post(f"/consultations/{c['id']}/addenda", {"reason": "Lab result received", "content": "CBC normal."})
    assert a.status_code == 201
    assert a.json()["addenda"][0]["content"] == "CBC normal."
    assert a.json()["chiefComplaint"] == "Fever and sore throat for 3 days"  # original unchanged


def test_other_doctor_cannot_edit_or_start(started, other_doctor, doctor, receptionist, new_patient):
    c = started["consultation"]
    r = other_doctor.put(f"/consultations/{c['id']}", full_note(c["version"]))
    assert r.status_code == 403
    # but can read for continuity of care
    assert other_doctor.get(f"/consultations/{c['id']}").status_code == 200
    a = receptionist.post("/appointments/walk-in", {"patientId": new_patient()["id"], "doctorId": doctor.doctor_id}).json()
    assert other_doctor.post(f"/appointments/{a['id']}/start-consultation").status_code == 403


def test_receptionist_cannot_access_consultation(started, receptionist):
    assert receptionist.get(f"/consultations/{started['consultation']['id']}").status_code == 403
    assert receptionist.post(f"/appointments/{started['appointment']['id']}/start-consultation").status_code == 403


def test_patient_timeline_shows_history(started, doctor):
    c = started["consultation"]
    doctor.put(f"/consultations/{c['id']}", full_note(c["version"]))
    doctor.post(f"/consultations/{c['id']}/finalize")
    tl = doctor.get(f"/patients/{started['patient']['id']}/timeline").json()
    assert tl[0]["consultationId"] == c["id"]
    assert tl[0]["diagnoses"] == ["J02.9 Acute pharyngitis, unspecified"]
    assert tl[0]["vitals"]["pulse"] == 88


def test_stale_version_conflict(started, doctor):
    c = started["consultation"]
    assert doctor.put(f"/consultations/{c['id']}", full_note(c["version"])).status_code == 200
    r = doctor.put(f"/consultations/{c['id']}", full_note(c["version"]))
    assert r.status_code == 409 and r.json()["error"]["code"] == "STALE_VERSION"


def test_diagnosis_catalogue_search(doctor):
    res = doctor.get("/catalogue/diagnoses", params={"q": "hypertension"}).json()
    assert res[0]["code"] == "I10"
