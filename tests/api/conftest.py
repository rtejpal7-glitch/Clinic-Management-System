"""
Shared fixtures for ClinicFlow API integration tests.

These tests run against a live server + PostgreSQL (see docs/TESTING.md).
They create their own uniquely-named users, doctor and patients, so they can
run repeatedly against the same development database.

Environment:
  CF_API_URL          default http://localhost:8000/api/v1
  CF_ADMIN_USERNAME   default admin
  CF_ADMIN_PASSWORD   default Clinic-Demo-2026
"""
import os
import random
import string
import datetime as dt
from zoneinfo import ZoneInfo

import pytest
import requests

API = os.environ.get("CF_API_URL", "http://localhost:8000/api/v1")
ADMIN_USER = os.environ.get("CF_ADMIN_USERNAME", "admin")
ADMIN_PASS = os.environ.get("CF_ADMIN_PASSWORD", "Clinic-Demo-2026")
IST = ZoneInfo("Asia/Kolkata")
PASSWORD = "Test-Passw0rd-2026"


def rid(n=6):
    return "".join(random.choices(string.ascii_lowercase + string.digits, k=n))


def rname(n=6):
    """Letters only — patient names may not contain digits."""
    return "".join(random.choices(string.ascii_lowercase, k=n)).title()


def rand_mobile():
    return random.choice("6789") + "".join(random.choices(string.digits, k=9))


class Client:
    """Tiny API client that keeps a bearer token."""

    def __init__(self, token=None):
        self.token = token

    def _h(self):
        return {"Authorization": f"Bearer {self.token}"} if self.token else {}

    def get(self, path, **kw):
        return requests.get(API + path, headers=self._h(), timeout=20, **kw)

    def post(self, path, json=None, **kw):
        return requests.post(API + path, json=json if json is not None else {}, headers=self._h(), timeout=20, **kw)

    def put(self, path, json=None, **kw):
        return requests.put(API + path, json=json if json is not None else {}, headers=self._h(), timeout=20, **kw)

    def delete(self, path, **kw):
        return requests.delete(API + path, headers=self._h(), timeout=20, **kw)


def login(username, password):
    r = requests.post(API + "/auth/login", json={"username": username, "password": password}, timeout=20)
    assert r.status_code == 200, r.text
    return Client(r.json()["token"])


def create_user(admin, role, password=PASSWORD):
    """Create a user and complete the forced first-login password change."""
    username = f"{role.lower()}_{rid()}"
    temp = "Temp-Pass-" + rid(8) + "1"
    r = admin.post("/users", {"username": username, "fullName": f"Test {role.title()} {rname(3)}", "password": temp, "roles": [role]})
    assert r.status_code == 201, r.text
    c = login(username, temp)
    r = c.post("/auth/change-password", {"currentPassword": temp, "newPassword": password})
    assert r.status_code == 200, r.text
    c = login(username, password)
    c.username = username
    c.user_id = next(u["id"] for u in admin.get("/users").json() if u["username"] == username)
    return c


@pytest.fixture(scope="session")
def admin():
    return login(ADMIN_USER, ADMIN_PASS)


@pytest.fixture(scope="session")
def receptionist(admin):
    return create_user(admin, "RECEPTIONIST")


@pytest.fixture(scope="session")
def doctor(admin):
    """A doctor user with a profile and an all-week schedule (00:00-23:45, 15-minute slots)."""
    c = create_user(admin, "DOCTOR")
    r = admin.post("/doctors", {"userId": c.user_id, "displayName": f"Dr. Test {rid(4)}", "qualification": "MBBS",
                                "registrationNumber": f"KMC-{rid(5)}", "consultationFeePaise": 50000})
    assert r.status_code == 201, r.text
    c.doctor_id = r.json()["id"]
    blocks = [{"weekday": d, "startTime": "00:00", "endTime": "23:45", "slotMinutes": 15} for d in range(1, 8)]
    r = admin.put(f"/doctors/{c.doctor_id}/schedule", {"blocks": blocks})
    assert r.status_code == 200, r.text
    # Sign in again so the session picks up the newly created doctor profile.
    fresh = login(c.username, PASSWORD)
    fresh.username, fresh.user_id, fresh.doctor_id = c.username, c.user_id, c.doctor_id
    return fresh


@pytest.fixture(scope="session")
def other_doctor(admin):
    c = create_user(admin, "DOCTOR")
    r = admin.post("/doctors", {"userId": c.user_id, "displayName": f"Dr. Other {rid(4)}"})
    assert r.status_code == 201, r.text
    c.doctor_id = r.json()["id"]
    blocks = [{"weekday": d, "startTime": "00:00", "endTime": "23:45", "slotMinutes": 15} for d in range(1, 8)]
    assert admin.put(f"/doctors/{c.doctor_id}/schedule", {"blocks": blocks}).status_code == 200
    return c


def patient_payload(**over):
    p = {"fullName": f"Test Patient {rname()}", "dateOfBirth": f"19{random.randint(50, 99)}-0{random.randint(1, 9)}-1{random.randint(0, 9)}",
         "gender": random.choice(["MALE", "FEMALE"]), "mobile": rand_mobile(), "city": "Bengaluru", "state": "Karnataka",
         "pincode": "560001"}
    p.update(over)
    return p


@pytest.fixture
def new_patient(receptionist):
    def make(**over):
        r = receptionist.post("/patients", patient_payload(**over))
        if r.status_code == 409:  # extremely unlikely random collision
            body = patient_payload(**over)
            body.update({"confirmNotDuplicate": True, "duplicateOverrideReason": "test data collision"})
            r = receptionist.post("/patients", body)
        assert r.status_code == 201, r.text
        return r.json()
    return make


def future_slot(c, doctor_id, days_ahead=1, index=0):
    """Return the startAt of an available slot on a future date."""
    for d in range(days_ahead, days_ahead + 14):
        date = (dt.datetime.now(IST) + dt.timedelta(days=d)).date().isoformat()
        slots = [s for s in c.get(f"/doctors/{doctor_id}/slots", params={"date": date}).json() if s["available"]]
        if len(slots) > index:
            return slots[index]["startAt"]
    raise AssertionError("no free slot found")


def today_slot(c, doctor_id):
    date = dt.datetime.now(IST).date().isoformat()
    slots = [s for s in c.get(f"/doctors/{doctor_id}/slots", params={"date": date}).json() if s["available"]]
    return slots[0]["startAt"] if slots else None


@pytest.fixture(scope="session")
def clinic_b(admin):
    """A second clinic with its own administrator (created by the platform admin)."""
    code = "T" + rid(5).upper()
    temp = "Temp-Pass-" + rid(8) + "1"
    username = f"badmin_{rid()}"
    r = admin.post("/facilities", {"code": code, "name": f"Test Skin Clinic {code}", "specialty": "DERMATOLOGY",
                                   "city": "Mysuru", "state": "Karnataka", "adminUsername": username,
                                   "adminFullName": "Clinic B Admin", "adminPassword": temp})
    assert r.status_code == 201, r.text
    fac = r.json()
    c = login(username, temp)
    assert c.post("/auth/change-password", {"currentPassword": temp, "newPassword": PASSWORD}).status_code == 200
    c = login(username, PASSWORD)
    c.facility = fac
    c.username = username
    return c


def make_doctor(admin, prefix="Dr. Derm"):
    """Doctor with profile + all-week schedule in the admin's clinic; returns a fresh logged-in client."""
    c = create_user(admin, "DOCTOR")
    r = admin.post("/doctors", {"userId": c.user_id, "displayName": f"{prefix} {rname(4)}", "qualification": "MD (DVL)",
                                "registrationNumber": f"KMC-{rid(5)}", "consultationFeePaise": 60000})
    assert r.status_code == 201, r.text
    doctor_id = r.json()["id"]
    blocks = [{"weekday": d, "startTime": "00:00", "endTime": "23:45", "slotMinutes": 15} for d in range(1, 8)]
    assert admin.put(f"/doctors/{doctor_id}/schedule", {"blocks": blocks}).status_code == 200
    fresh = login(c.username, PASSWORD)
    fresh.username, fresh.user_id, fresh.doctor_id = c.username, c.user_id, doctor_id
    return fresh


def find_med(c, generic_code):
    """Generic catalogue product for a generic code (via the content list is reviewer-only, so search by name)."""
    names = {"FEXOFENADINE": "fexofenadine", "BILASTINE": "bilastine", "CETIRIZINE": "cetirizine", "LULICONAZOLE": "luliconazole",
             "TERBINAFINE": "terbinafine", "ISOTRETINOIN": "isotretinoin", "DOXYCYCLINE": "doxycycline", "ADAPALENE": "adapalene",
             "PERMETHRIN": "permethrin", "LEVOCETIRIZINE": "levocetirizine", "MUPIROCIN": "mupirocin", "ITRACONAZOLE": "itraconazole"}
    for m in c.get("/catalogue/medications", params={"q": names.get(generic_code, generic_code.lower())}).json():
        if m.get("genericCode") == generic_code and not m.get("brandName"):
            return m
    raise AssertionError(f"catalogue has no product for {generic_code}")


def rx_item(m, dose="1 tablet", frequency="Once daily", days=14, **extra):
    it = {"medicationId": m["id"], "medicineName": m["genericName"], "strength": m["strength"], "dosageForm": m["dosageForm"],
          "dose": dose, "route": m["defaultRoute"], "frequency": frequency, "durationDays": days}
    it.update(extra)
    return it


URTICARIA_ASSESSMENT = {"problemType": "HIVES_SWELLING", "durationValue": 5, "durationUnit": "DAYS", "severity": "MODERATE",
                        "symptoms": ["ITCHING"], "lesionTypes": ["WHEAL"], "bodySites": ["ARMS", "BACK"],
                        "distribution": "GENERALISED", "pregnancyStatus": "NOT_APPLICABLE"}


def derm_visit(recep, doctor, patient=None, assessment=None, dx=("L50.9", "Urticaria, unspecified"), items=None,
               finalize=True, override=None):
    """Walk-in -> start -> notes + derm assessment -> prescription -> (finalize). Returns (consultation, patient)."""
    if patient is None:
        r = recep.post("/patients", patient_payload())
        if r.status_code == 409:
            body = patient_payload()
            body.update({"confirmNotDuplicate": True, "duplicateOverrideReason": "test data collision"})
            r = recep.post("/patients", body)
        assert r.status_code == 201, r.text
        patient = r.json()
    a = recep.post("/appointments/walk-in", {"patientId": patient["id"], "doctorId": doctor.doctor_id})
    assert a.status_code == 201, a.text
    c = doctor.post(f"/appointments/{a.json()['id']}/start-consultation").json()
    r = doctor.put(f"/consultations/{c['id']}", {"version": c["version"], "chiefComplaint": "Itchy skin rash",
                                                "diagnoses": [{"codeSystem": "ICD-10", "code": dx[0], "display": dx[1]}]})
    assert r.status_code == 200, r.text
    r = doctor.put(f"/consultations/{c['id']}/derm", assessment or URTICARIA_ASSESSMENT)
    assert r.status_code == 200, r.text
    if items is not None:
        r = doctor.put(f"/consultations/{c['id']}/prescription", {"items": items})
        assert r.status_code == 200, r.text
    if finalize:
        body = {"safetyOverrideReason": override} if override else {}
        r = doctor.post(f"/consultations/{c['id']}/finalize", body)
        assert r.status_code == 200, r.text
        c = r.json()
    return c, patient
