"""Authentication, RBAC, audit logging and sensitive-data access tests."""
import requests

from conftest import API, PASSWORD, Client, create_user, login, rid


def test_invalid_login_is_rejected_with_generic_message():
    r = requests.post(API + "/auth/login", json={"username": "admin", "password": "wrong-password-1"})
    assert r.status_code == 401
    assert r.json()["error"]["code"] == "INVALID_CREDENTIALS"
    r2 = requests.post(API + "/auth/login", json={"username": "no_such_user_x", "password": "whatever-123"})
    assert r2.status_code == 401
    # Same message for unknown user and wrong password (no user enumeration)
    assert r.json()["error"]["message"] == r2.json()["error"]["message"]


def test_account_lockout_after_repeated_failures(admin):
    c = create_user(admin, "RECEPTIONIST")
    for _ in range(5):
        requests.post(API + "/auth/login", json={"username": c.username, "password": "bad-password-9"})
    r = requests.post(API + "/auth/login", json={"username": c.username, "password": PASSWORD})
    assert r.status_code == 423
    assert r.json()["error"]["code"] == "ACCOUNT_LOCKED"
    # Admin password reset clears the lock
    uid = c.user_id
    assert admin.post(f"/users/{uid}/reset-password", {"newPassword": "Reset-Pass-2026x"}).status_code == 200
    assert requests.post(API + "/auth/login", json={"username": c.username, "password": "Reset-Pass-2026x"}).status_code == 200


def test_unauthenticated_requests_are_rejected():
    for path in ["/patients", "/appointments", "/bills", "/audit-logs", "/users", "/dashboard/summary"]:
        r = requests.get(API + path)
        assert r.status_code == 401, path
    r = requests.get(API + "/patients", headers={"Authorization": "Bearer forged-token-value-123456789"})
    assert r.status_code == 401


def test_logout_revokes_token(admin):
    c = create_user(admin, "RECEPTIONIST")
    assert c.get("/auth/me").status_code == 200
    assert c.post("/auth/logout").status_code == 200
    assert c.get("/auth/me").status_code == 401


def test_temporary_password_must_be_changed_first(admin):
    username = f"tmp_{rid()}"
    admin.post("/users", {"username": username, "fullName": "Temp User", "password": "Temp-Pass-12345", "roles": ["RECEPTIONIST"]})
    c = login(username, "Temp-Pass-12345")
    assert c.get("/auth/me").json()["mustChangePassword"] is True
    r = c.get("/patients")
    assert r.status_code == 403 and r.json()["error"]["code"] == "PASSWORD_CHANGE_REQUIRED"


def test_password_policy_enforced(admin):
    r = admin.post("/users", {"username": f"weak_{rid()}", "fullName": "Weak", "password": "short", "roles": ["RECEPTIONIST"]})
    assert r.status_code == 400


def test_receptionist_cannot_access_clinical_or_admin_apis(receptionist, admin):
    assert receptionist.get("/users").status_code == 403
    assert receptionist.get("/audit-logs").status_code == 403
    assert receptionist.put("/settings", {"clinicName": "Hacked"}).status_code == 403
    pid = receptionist.get("/patients").json()["items"][0]["id"] if receptionist.get("/patients").json()["items"] else None
    if pid:
        assert receptionist.get(f"/patients/{pid}/timeline").status_code == 403  # clinical history
        assert receptionist.get(f"/patients/{pid}/allergies").status_code == 403


def test_doctor_cannot_manage_billing_or_users(doctor):
    assert doctor.get("/bills").status_code == 403
    assert doctor.post("/bills", {"patientId": "00000000-0000-0000-0000-000000000000", "items": []}).status_code == 403
    assert doctor.get("/users").status_code == 403
    assert doctor.get("/reports/billing/daily", params={"date": "2026-01-01"}).status_code == 403


def test_admin_cannot_write_clinical_notes(admin):
    # Admin has no clinical permissions at all (least privilege)
    assert admin.put("/consultations/00000000-0000-0000-0000-000000000000", {}).status_code == 403
    assert admin.get("/consultations/00000000-0000-0000-0000-000000000000").status_code == 403


def test_dashboard_hides_financials_without_permission(doctor, admin):
    d = doctor.get("/dashboard/summary").json()
    assert "revenueTodayPaise" not in d and "pendingPaymentsPaise" not in d
    a = admin.get("/dashboard/summary").json()
    assert "revenueTodayPaise" in a


def test_denied_access_is_audited(receptionist, admin):
    receptionist.get("/audit-logs")
    logs = admin.get("/audit-logs", params={"action": "ACCESS_DENIED", "size": 20}).json()["items"]
    assert any(l["username"] == receptionist.username for l in logs if "username" in l) or \
           any(l.get("userId") for l in logs)


def test_login_events_are_audited(admin):
    requests.post(API + "/auth/login", json={"username": "admin", "password": "definitely-wrong-1"})
    logs = admin.get("/audit-logs", params={"action": "AUTH_LOGIN", "outcome": "FAILURE", "size": 5}).json()["items"]
    assert len(logs) >= 1


def test_audit_log_hash_chain_verifies(admin):
    v = admin.get("/audit-logs/verify").json()
    assert v["valid"] is True
    assert v["checkedRecords"] > 0


def test_role_permission_changes_take_effect_and_are_audited(admin):
    code = "AUDITOR_" + rid(4).upper()
    r = admin.post("/roles", {"code": code, "name": "Auditor", "permissions": ["audit.read"]})
    assert r.status_code == 201, r.text
    role_id = r.json()["id"]
    username = f"aud_{rid()}"
    admin.post("/users", {"username": username, "fullName": "Audit Person", "password": "Temp-Pass-12345", "roles": [code]})
    c = login(username, "Temp-Pass-12345")
    c.post("/auth/change-password", {"currentPassword": "Temp-Pass-12345", "newPassword": PASSWORD})
    c = login(username, PASSWORD)
    assert c.get("/audit-logs").status_code == 200
    assert c.get("/patients").status_code == 403  # deny by default
    admin.put(f"/roles/{role_id}", {"permissions": []})
    assert c.get("/audit-logs").status_code == 403
    logs = admin.get("/audit-logs", params={"action": "ROLE_PERMISSIONS_UPDATE", "entityId": role_id}).json()["items"]
    assert len(logs) == 1


def test_cannot_remove_last_user_manager(admin):
    roles = admin.get("/roles").json()
    admin_role = next(r for r in roles if r["code"] == "ADMIN")
    perms = [p for p in admin_role["permissions"] if p != "user.manage"]
    r = admin.put(f"/roles/{admin_role['id']}", {"permissions": perms})
    assert r.status_code == 409 and r.json()["error"]["code"] == "LAST_ADMIN"


def test_patient_view_is_access_logged(receptionist, admin, new_patient):
    p = new_patient()
    receptionist.get(f"/patients/{p['id']}")
    logs = admin.get("/audit-logs", params={"action": "PATIENT_VIEW", "patientId": p["id"]}).json()["items"]
    assert len(logs) >= 1


def test_security_headers_present():
    r = requests.get(API + "/health")
    assert r.headers.get("Cache-Control") == "no-store"
    assert r.headers.get("X-Content-Type-Options") == "nosniff"
    assert r.headers.get("X-Frame-Options") == "DENY"


def test_malformed_json_returns_400(admin):
    r = requests.post(API + "/patients", data="{not json", headers={"Authorization": f"Bearer {admin.token}",
                                                                     "Content-Type": "application/json"})
    assert r.status_code == 400


def test_sql_injection_in_search_is_harmless(receptionist):
    r = receptionist.get("/patients", params={"q": "x' OR '1'='1"})
    assert r.status_code == 200
    assert r.json()["total"] == 0


def test_keepalive_bodies_on_bodyless_endpoints_do_not_corrupt_next_request(admin):
    """Regression: a JSON body sent to an endpoint that takes none must be drained,
    otherwise the leftover bytes corrupt the next request on the same connection."""
    s = requests.Session()
    s.headers["Authorization"] = f"Bearer {create_user(admin, 'RECEPTIONIST').token}"
    for _ in range(3):
        assert s.post(API + "/auth/me", json={}).status_code in (404, 405)  # wrong method, with a body
        assert s.get(API + "/auth/me").status_code == 200
    r = s.post(API + "/auth/logout", json={"unexpected": "body"})
    assert r.status_code == 200
    assert s.post(API + "/auth/login", json={"username": "admin", "password": "x-wrong-123"}).status_code == 401


def test_oversized_body_rejected(admin):
    big = {"fullName": "x" * (1100 * 1024)}
    r = requests.post(API + "/patients", json=big, headers={"Authorization": f"Bearer {admin.token}"})
    assert r.status_code == 413
