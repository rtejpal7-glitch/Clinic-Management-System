"""Patient registration, IDs, duplicate detection, search, updates and history."""
import re

from conftest import patient_payload, rand_mobile, rname


def test_register_patient_generates_unique_id(receptionist):
    a = receptionist.post("/patients", patient_payload()).json()
    b = receptionist.post("/patients", patient_payload()).json()
    pat = re.compile(r"^[A-Z]{2,6}-\d{4}-\d{6}$")
    assert pat.match(a["patientNumber"]) and pat.match(b["patientNumber"])
    assert a["patientNumber"] != b["patientNumber"]
    assert a["id"] != b["id"]
    assert a["abhaLinked"] is False


def test_registration_validation(receptionist):
    r = receptionist.post("/patients", {"fullName": "A", "dateOfBirth": "2099-01-01", "gender": "X", "mobile": "12345"})
    assert r.status_code == 400
    fields = r.json()["error"]["fields"]
    assert {"fullName", "gender", "mobile"} <= set(fields)
    r = receptionist.post("/patients", patient_payload(dateOfBirth="2099-01-01"))
    assert r.status_code == 400 and "dateOfBirth" in r.json()["error"]["fields"]


def test_duplicate_detection_blocks_then_allows_with_reason(receptionist):
    surname = rname(8)
    base = patient_payload(fullName=f"Lakshmi {surname}", dateOfBirth="1968-04-12", mobile=rand_mobile())
    first = receptionist.post("/patients", base)
    assert first.status_code == 201
    # pre-check endpoint
    chk = receptionist.post("/patients/duplicate-check", {**base, "fullName": f"Laxmi {surname}"}).json()
    assert chk["possibleDuplicates"] is True
    assert chk["candidates"][0]["patient"]["id"] == first.json()["id"]
    # registration is blocked with candidates
    dup = receptionist.post("/patients", {**base, "fullName": f"Laxmi {surname}"})
    assert dup.status_code == 409
    err = dup.json()["error"]
    assert err["code"] == "POSSIBLE_DUPLICATE"
    assert err["data"]["candidates"][0]["id"] == first.json()["id"]
    assert "mobile" in err["data"]["candidates"][0]["matchedOn"]
    # override without reason is refused, with reason succeeds
    assert receptionist.post("/patients", {**base, "confirmNotDuplicate": True}).status_code == 400
    ok = receptionist.post("/patients", {**base, "confirmNotDuplicate": True, "duplicateOverrideReason": "Twin sibling, verified ID"})
    assert ok.status_code == 201


def test_family_sharing_mobile_is_not_flagged(receptionist):
    mobile, family = rand_mobile(), rname(8)
    assert receptionist.post("/patients", patient_payload(fullName=f"Ramesh {family}", dateOfBirth="1975-01-01", mobile=mobile)).status_code == 201
    child = receptionist.post("/patients", patient_payload(fullName=f"Kavya {family}", dateOfBirth="2012-06-06", mobile=mobile))
    assert child.status_code == 201


def test_search_by_number_name_and_mobile(receptionist, new_patient):
    p = new_patient(fullName="Zubin Mehta Testcase")
    by_num = receptionist.get("/patients", params={"q": p["patientNumber"]}).json()
    assert by_num["total"] == 1 and by_num["items"][0]["id"] == p["id"]
    by_mobile = receptionist.get("/patients", params={"q": p["mobile"]}).json()
    assert any(i["id"] == p["id"] for i in by_mobile["items"])
    by_name = receptionist.get("/patients", params={"q": "zubin mehta test"}).json()
    assert any(i["id"] == p["id"] for i in by_name["items"])


def test_update_records_history_and_uses_optimistic_locking(receptionist, admin, new_patient):
    p = new_patient()
    upd = {**patient_payload(), "fullName": p["fullName"], "dateOfBirth": p["dateOfBirth"], "gender": p["gender"],
           "mobile": p["mobile"], "city": "Mysuru", "version": p["version"], "changeReason": "Moved"}
    r = receptionist.put(f"/patients/{p['id']}", upd)
    assert r.status_code == 200, r.text
    assert r.json()["city"] == "Mysuru" and r.json()["version"] == p["version"] + 1
    assert r.json()["patientNumber"] == p["patientNumber"]  # immutable
    # stale version is rejected
    stale = receptionist.put(f"/patients/{p['id']}", {**upd, "city": "Hubli"})
    assert stale.status_code == 409 and stale.json()["error"]["code"] == "STALE_VERSION"
    hist = admin.get(f"/patients/{p['id']}/history").json()
    assert any(h["fieldName"] == "city" and h["newValue"] == "Mysuru" and h["reason"] == "Moved" for h in hist)


def test_unauthorized_modification_attempt(doctor, admin, new_patient):
    p = new_patient()
    # A custom role without patient.update cannot modify demographics
    code = "VIEWER_X"
    roles = {r["code"]: r for r in admin.get("/roles").json()}
    if code not in roles:
        admin.post("/roles", {"code": code, "name": "Viewer", "permissions": ["patient.read"]})
    from conftest import login, rid, PASSWORD
    u = f"viewer_{rid()}"
    admin.post("/users", {"username": u, "fullName": "Read Only", "password": "Temp-Pass-12345", "roles": [code]})
    c = login(u, "Temp-Pass-12345")
    c.post("/auth/change-password", {"currentPassword": "Temp-Pass-12345", "newPassword": PASSWORD})
    c = login(u, PASSWORD)
    assert c.get(f"/patients/{p['id']}").status_code == 200
    r = c.put(f"/patients/{p['id']}", {**p, "city": "Nowhere"})
    assert r.status_code == 403


def test_allergies_require_clinical_permission(doctor, receptionist, new_patient):
    p = new_patient()
    assert receptionist.post(f"/patients/{p['id']}/allergies", {"substance": "Penicillin", "category": "MEDICATION"}).status_code == 403
    r = doctor.post(f"/patients/{p['id']}/allergies", {"substance": "Penicillin", "category": "MEDICATION", "severity": "SEVERE",
                                                       "reaction": "Rash"})
    assert r.status_code == 201
    lst = doctor.get(f"/patients/{p['id']}/allergies").json()
    assert lst[0]["substance"] == "Penicillin"
    upd = doctor.put(f"/patients/{p['id']}/allergies/{lst[0]['id']}", {"status": "ENTERED_IN_ERROR"})
    assert upd.status_code == 200 and upd.json()["status"] == "ENTERED_IN_ERROR"


def test_unknown_patient_returns_404(receptionist):
    assert receptionist.get("/patients/00000000-0000-0000-0000-000000000000").status_code == 404
    assert receptionist.get("/patients/not-a-uuid").status_code == 404
