"""Multi-clinic isolation: one installation hosts several clinics, separated by
PostgreSQL row-level security plus explicit scoping of users/roles/audit."""
import pytest

from conftest import PASSWORD, Client, login, patient_payload, rid, create_user


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


def test_me_reports_facility(admin, clinic_b):
    me = admin.get("/auth/me").json()
    assert me["facilityCode"] == "MAIN" and me["specialty"] in ("DERMATOLOGY", "GENERAL")
    meb = clinic_b.get("/auth/me").json()
    assert meb["facilityId"] == clinic_b.facility["id"] and meb["facilityId"] != me["facilityId"]


def test_only_platform_admin_creates_clinics(clinic_b):
    r = clinic_b.post("/facilities", {"code": "XX" + rid(3).upper(), "name": "Nope", "adminUsername": "x_" + rid(),
                                      "adminFullName": "X", "adminPassword": "Whatever-12345"})
    assert r.status_code == 403
    # A clinic admin sees only its own clinic
    lst = clinic_b.get("/facilities").json()
    assert [f["id"] for f in lst] == [clinic_b.facility["id"]]


def test_patients_isolated_between_clinics(receptionist, clinic_b):
    p = receptionist.post("/patients", patient_payload()).json()
    rec_b = create_user(clinic_b, "RECEPTIONIST")
    # Not readable by id, not searchable, not in lists
    assert rec_b.get(f"/patients/{p['id']}").status_code == 404
    assert all(x["id"] != p["id"] for x in rec_b.get("/patients", params={"q": p["patientNumber"]}).json()["items"])
    # The same person registering at clinic B is not flagged as a duplicate of clinic A's record
    body = patient_payload(fullName=p["fullName"], mobile=p["mobile"], dateOfBirth=p["dateOfBirth"], gender=p["gender"])
    r = rec_b.post("/patients", body)
    assert r.status_code == 201, r.text
    assert r.json()["id"] != p["id"]


def test_users_roles_and_audit_scoped(admin, clinic_b):
    a_users = {u["username"] for u in admin.get("/users").json()}
    b_users = {u["username"] for u in clinic_b.get("/users").json()}
    assert clinic_b.username in b_users and clinic_b.username not in a_users
    assert "admin" not in b_users
    # Clinic B cannot touch a user of clinic A
    a_user_id = next(u["id"] for u in admin.get("/users").json() if u["username"] == "admin")
    assert clinic_b.put(f"/users/{a_user_id}", {"fullName": "Hijack"}).status_code == 404
    # Shared system roles cannot be changed by a clinic admin
    doctor_role = next(r for r in clinic_b.get("/roles").json() if r["code"] == "DOCTOR")
    assert clinic_b.put(f"/roles/{doctor_role['id']}", {"permissions": ["patient.read"]}).status_code == 403
    # Cannot grant the platform role
    r = clinic_b.post("/users", {"username": "plat_" + rid(), "fullName": "Escalate", "password": "Temp-Pass-12345x",
                                 "roles": ["PLATFORM_ADMIN"]})
    assert r.status_code == 403
    # Audit entries are per clinic
    actions_b = {l["action"] for l in clinic_b.get("/audit-logs", params={"size": 200}).json()["items"]}
    assert "FACILITY_CREATE" not in actions_b  # recorded in the platform admin's clinic


def test_custom_role_is_clinic_local(admin, clinic_b):
    code = "NURSE_" + rid(4).upper()
    r = clinic_b.post("/roles", {"code": code, "name": "Nurse B", "permissions": ["patient.read"]})
    assert r.status_code == 201, r.text
    assert code not in {x["code"] for x in admin.get("/roles").json()}
    r = clinic_b.post("/roles", {"code": "PX_" + rid(4).upper(), "name": "Bad", "permissions": ["platform.manage"]})
    assert r.status_code == 400


def test_settings_identity_per_clinic(admin, clinic_b):
    sa = admin.get("/settings").json()
    sb = clinic_b.get("/settings").json()
    assert sb["clinicName"] == clinic_b.facility["name"] and sa["clinicName"] != sb["clinicName"]
    sb["phone"] = "0821" + rid(6).translate(str.maketrans("abcdefghijklmnopqrstuvwxyz", "12345678901234567890123456"))[:6]
    upd = {k: sb[k] for k in ("clinicName", "address", "city", "state", "pincode", "phone", "email") if sb.get(k)}
    r = clinic_b.put("/settings", upd)
    assert r.status_code == 200, r.text
    assert admin.get("/settings").json()["clinicName"] == sa["clinicName"]
    # Installation-wide values are reserved to the platform administrator
    r = clinic_b.put("/settings", {**upd, "sessionIdleMinutes": 45})
    assert r.status_code == 403


def test_billing_services_per_clinic(admin, clinic_b):
    a = {s["code"] for s in admin.get("/billing/services").json()}
    b = {s["code"] for s in clinic_b.get("/billing/services").json()}
    assert a and a == b or b.issubset(a)  # starter copy


def test_network_sharing_requires_agreement(clinic_b):
    r = clinic_b.put("/facilities/current/network", {"enabled": True})
    assert r.status_code == 400
    r = clinic_b.put("/facilities/current/network", {"enabled": True, "agreementRef": "DSA-TEST-001"})
    assert r.status_code == 200 and r.json()["networkSharingEnabled"] is True
    logs = clinic_b.get("/audit-logs", params={"action": "NETWORK_SHARING_ENABLE"}).json()["items"]
    assert logs and logs[0]["outcome"] == "SUCCESS"


def test_rls_blocks_direct_cross_clinic_ids(doctor, clinic_b):
    """Even with a valid id from another clinic, endpoints behave as if it does not exist."""
    doc_b = create_user(clinic_b, "DOCTOR")
    appts = doctor.get("/appointments").json()
    items = appts["items"] if isinstance(appts, dict) else appts
    for a in items[:3]:
        assert doc_b.get(f"/appointments/{a['id']}").status_code in (403, 404)
