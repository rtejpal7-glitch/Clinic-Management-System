# API reference

Base path `/api/v1`. All endpoints except `POST /auth/login` and `GET /health` need `Authorization: Bearer <token>`. The full machine-readable spec is in [api/openapi.json](api/openapi.json). Interactive Swagger UI is at `/swagger/ui` on a running server.

## Conventions

- JSON in and out (`application/fhir+json` for FHIR endpoints). Money is in **integer paise**. Timestamps are RFC 3339 UTC (`2026-11-07T15:11:55.123Z`). Dates are `YYYY-MM-DD` in the clinic timezone.
- Errors use one envelope: `{"error": {"status": 409, "code": "SLOT_UNAVAILABLE", "message": "…", "fields": {"field": "message"}, "data": {…}}}`.
- Common codes: `VALIDATION_ERROR` (400), `UNAUTHENTICATED` / `SESSION_INVALID` / `INVALID_CREDENTIALS` (401), `FORBIDDEN` / `PASSWORD_CHANGE_REQUIRED` (403), `NOT_FOUND` (404), `POSSIBLE_DUPLICATE`, `SLOT_UNAVAILABLE`, `STALE_VERSION`, `RECORD_LOCKED`, `INVALID_STATE`, `ALREADY_BILLED` (409), `PAYLOAD_TOO_LARGE` (413), `SLOT_OUTSIDE_SCHEDULE`, `ABHA_NOT_LINKED`, `NO_ACTIVE_CONSENT` (422), `ACCOUNT_LOCKED` (423).
- Updates that can conflict take a `version` (optimistic locking).

## Endpoints (84 operations)


### System

| Method | Path | Description |
|---|---|---|
| GET | `/health` | Liveness / readiness probe |

### Auth

| Method | Path | Description |
|---|---|---|
| POST | `/auth/change-password` | Change own password (revokes other sessions) |
| POST | `/auth/login` | Sign in with username and password; returns a bearer token |
| POST | `/auth/logout` | Sign out (revokes the current session) |
| GET | `/auth/me` | Current user, roles and effective permissions |

### Users & Roles

| Method | Path | Description |
|---|---|---|
| GET | `/permissions` | List all permission codes |
| GET | `/roles` | List roles with their permissions |
| POST | `/roles` | Create a custom role |
| PUT | `/roles/{id}` | Update role name/description and permission mapping |
| GET | `/users` | List users |
| POST | `/users` | Create a user (must change password on first sign-in) |
| PUT | `/users/{id}` | Update user profile, roles or active status |
| POST | `/users/{id}/reset-password` | Reset a user's password (revokes their sessions) |

### Settings

| Method | Path | Description |
|---|---|---|
| GET | `/settings` | Clinic configuration |
| PUT | `/settings` | Update clinic configuration |

### Dashboard

| Method | Path | Description |
|---|---|---|
| GET | `/dashboard/summary` | Operational dashboard metrics (financial figures only with billing permissions) |

### Patients

| Method | Path | Description |
|---|---|---|
| GET | `/patients` | Search patients by name, mobile, patient number or ABHA number/address (q, page, size) |
| POST | `/patients` | Register a patient. Returns 409 POSSIBLE_DUPLICATE with candidates unless confirmNotDuplicate + reason are given |
| POST | `/patients/duplicate-check` | Check for possible duplicate patients before registration |
| GET | `/patients/{id}` | Patient demographics (access is audit-logged) |
| PUT | `/patients/{id}` | Update demographics (optimistic locking via version; field changes recorded in history) |
| GET | `/patients/{id}/allergies` | Patient allergies |
| POST | `/patients/{id}/allergies` | Record an allergy |
| PUT | `/patients/{id}/allergies/{allergyId}` | Change allergy status (ACTIVE / INACTIVE / ENTERED_IN_ERROR) |
| GET | `/patients/{id}/appointments` | Appointment history for a patient |
| GET | `/patients/{id}/bills` | Billing history for a patient |
| GET | `/patients/{id}/history` | Demographic change history |
| GET | `/patients/{id}/timeline` | Clinical history: consultations, diagnoses, medicines, orders, vitals (newest first) |

### Doctors & Schedules

| Method | Path | Description |
|---|---|---|
| GET | `/doctors` | List doctors (includeInactive=true for all) |
| POST | `/doctors` | Create a doctor profile linked to a user with the DOCTOR role |
| PUT | `/doctors/{id}` | Update a doctor profile |
| GET | `/doctors/{id}/schedule` | Weekly working hours of a doctor |
| PUT | `/doctors/{id}/schedule` | Replace a doctor's weekly working hours and slot lengths |
| GET | `/doctors/{id}/slots` | Slot availability for a doctor on a date (date=YYYY-MM-DD, clinic timezone) |

### Appointments

| Method | Path | Description |
|---|---|---|
| GET | `/appointments` | List appointments (filters: date, from, to, doctorId, patientId, status) |
| POST | `/appointments` | Book an appointment in a free slot (double-booking is rejected with 409) |
| POST | `/appointments/walk-in` | Register a walk-in: creates a checked-in visit and issues a queue token |
| GET | `/appointments/{id}` | Get an appointment |
| POST | `/appointments/{id}/cancel` | Cancel an appointment (reason required) |
| POST | `/appointments/{id}/check-in` | Check a patient in for today's appointment and issue a queue token |
| GET | `/appointments/{id}/history` | Status history of an appointment |
| POST | `/appointments/{id}/no-show` | Mark an appointment as no-show (after its start time) |
| POST | `/appointments/{id}/reschedule` | Move a scheduled appointment to another free slot |

### Queue

| Method | Path | Description |
|---|---|---|
| GET | `/queue` | Waiting queue for a date (default today) and optional doctor |
| POST | `/queue/{id}/call` | Call a token to the consultation room |
| POST | `/queue/{id}/skip` | Skip a token (patient not present) |

### Consultations

| Method | Path | Description |
|---|---|---|
| POST | `/appointments/{id}/start-consultation` | Start (or resume) the consultation for a checked-in appointment — treating doctor only |
| GET | `/consultations/{id}` | Full consultation record with vitals, diagnoses, prescription, orders and addenda (audit-logged) |
| PUT | `/consultations/{id}` | Save draft consultation notes, vitals and diagnoses (version required) |
| POST | `/consultations/{id}/addenda` | Add a correction/addendum to a finalized consultation (original is never modified) |
| POST | `/consultations/{id}/finalize` | Finalize (lock) the consultation and prescription; completes the appointment |
| PUT | `/consultations/{id}/prescription` | Save draft prescription items, advice and investigation orders (CPOE) |

### Catalogues

| Method | Path | Description |
|---|---|---|
| GET | `/catalogue/diagnoses` | Search diagnosis codes (ICD-10 starter list) |
| GET | `/catalogue/investigations` | Search the investigation catalogue (LOINC-coded where available) |
| GET | `/catalogue/medications` | Search the medicine catalogue (q, includeInactive) |
| POST | `/catalogue/medications` | Add a medicine to the catalogue |
| PUT | `/catalogue/medications/{id}` | Edit or deactivate a catalogue medicine |

### Billing

| Method | Path | Description |
|---|---|---|
| GET | `/billing/services` | Billable services catalogue (includeInactive=true for all) |
| POST | `/billing/services` | Add a billable service |
| PUT | `/billing/services/{id}` | Edit or deactivate a billable service |
| GET | `/bills` | List bills (date, status, patientId, q, page, size) |
| POST | `/bills` | Create a bill / invoice |
| GET | `/bills/{id}` | Get a bill with items and payments |
| POST | `/bills/{id}/cancel` | Cancel an unpaid bill (reason required) |
| POST | `/bills/{id}/payments` | Record a (partial) payment |
| POST | `/bills/{id}/refunds` | Record a refund (reason required in notes) |
| GET | `/reports/billing/daily` | Daily billing summary (date=YYYY-MM-DD, clinic timezone) |

### ABDM (mock)

| Method | Path | Description |
|---|---|---|
| POST | `/abdm/mock/inject-failure` | MOCK ONLY: make the next N gateway calls of an operation fail (for testing/demo) |
| GET | `/abdm/status` | ABDM integration mode (mock) and supported consent parameters |
| GET | `/abdm/transactions` | ABDM transaction log (optional patientId) |
| POST | `/abdm/transactions/{id}/retry` | Retry a failed consent request or health-information share |
| POST | `/abdm/transactions/{id}/verify-confirm` | Confirm ABHA linking with the OTP (mock: 123456 succeeds, 999999 simulates timeout) |
| POST | `/consents/{id}/mock-respond` | MOCK ONLY: simulate the patient granting or denying a consent request in their PHR app |
| POST | `/consents/{id}/revoke` | Record withdrawal / revocation of consent |
| POST | `/consultations/{id}/abdm-share` | Share a finalized consultation as an FHIR document bundle against a granted consent (mock push) |
| GET | `/patients/{id}/abha` | ABHA identifiers linked to a patient (masked) |
| POST | `/patients/{id}/abha/verify-init` | Start ABHA linking: gateway sends an OTP to the patient (mock: no OTP is sent) |
| POST | `/patients/{id}/abha/{identifierId}/unlink` | Unlink an ABHA identifier from the patient record |
| GET | `/patients/{id}/consents` | Consent records for a patient |
| POST | `/patients/{id}/consents` | Raise a consent request (requires a verified ABHA link) |

### FHIR

| Method | Path | Description |
|---|---|---|
| GET | `/fhir/Bundle/{id}/validate` | Local structural validation of the consultation FHIR bundle |
| GET | `/fhir/{type}/{id}` | Export a record as FHIR R4 JSON. type: Patient / Practitioner / Organization / Appointment / Encounter / Invoice / Bundle (consultation document) |

### Audit

| Method | Path | Description |
|---|---|---|
| GET | `/audit-logs` | Search audit log (filters: from, to, userId, action, entityType, entityId, patientId, outcome, page, size) |
| GET | `/audit-logs/verify` | Verify the audit hash chain (tamper evidence) |
