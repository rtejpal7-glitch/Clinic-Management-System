# API reference

Base path `/api/v1`. All endpoints except `POST /auth/login` and `GET /health` need `Authorization: Bearer <token>`. The full machine-readable spec is in [api/openapi.json](api/openapi.json). Interactive Swagger UI is at `/swagger/ui` on a running server.

## Conventions

- JSON in and out (`application/fhir+json` for FHIR endpoints). Money is in **integer paise**. Timestamps are RFC 3339 UTC (`2026-11-07T15:11:55.123Z`). Dates are `YYYY-MM-DD` in the clinic timezone.
- Errors use one envelope: `{"error": {"status": 409, "code": "SLOT_UNAVAILABLE", "message": "…", "fields": {"field": "message"}, "data": {…}}}`.
- Common codes: `VALIDATION_ERROR` (400), `UNAUTHENTICATED` / `SESSION_INVALID` / `INVALID_CREDENTIALS` (401), `FORBIDDEN` / `PASSWORD_CHANGE_REQUIRED` (403), `NOT_FOUND` (404), `POSSIBLE_DUPLICATE`, `SLOT_UNAVAILABLE`, `STALE_VERSION`, `RECORD_LOCKED`, `INVALID_STATE`, `ALREADY_BILLED` (409), `PAYLOAD_TOO_LARGE` (413), `SLOT_OUTSIDE_SCHEDULE`, `ABHA_NOT_LINKED`, `NO_ACTIVE_CONSENT` (422), `ACCOUNT_LOCKED` (423). v0.2 adds `SAFETY_ACKNOWLEDGEMENT_REQUIRED` (409, alerts in `data.alerts`; resend finalize with `safetyOverrideReason`), `SHARED_ROLE` / `INSTALLATION_SETTING` (403), `CLINIC_CODE_TAKEN`, `PHOTO_LIMIT` (409).
- Updates that can conflict take a `version` (optimistic locking).

## Endpoints (116 operations)


### Auth

| Method | Path | Description |
|---|---|---|
| POST | `/auth/change-password` | Change own password (revokes other sessions) |
| POST | `/auth/login` | Sign in with username and password; returns a bearer token |
| POST | `/auth/logout` | Sign out (revokes the current session) |
| GET | `/auth/me` | Current user, roles and effective permissions |

### System

| Method | Path | Description |
|---|---|---|
| GET | `/health` | Liveness / readiness probe |

### Dashboard

| Method | Path | Description |
|---|---|---|
| GET | `/dashboard/summary` | Operational dashboard metrics (financial figures only with billing permissions) |

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

### Audit

| Method | Path | Description |
|---|---|---|
| GET | `/audit-logs` | Search audit log (filters: from, to, userId, action, entityType, entityId, patientId, outcome, page, size) |
| GET | `/audit-logs/verify` | Verify the audit hash chain (tamper evidence) |

### Clinics

| Method | Path | Description |
|---|---|---|
| GET | `/facilities` | Clinics on this installation (platform admin: all; others: own clinic) |
| POST | `/facilities` | Create a clinic with its first administrator (platform administrators only) |
| GET | `/facilities/current` | The signed-in user's clinic |
| PUT | `/facilities/current/network` | Join or leave the de-identified ClinicFlow prescribing network (requires an agreement reference) |

### Dermatology

| Method | Path | Description |
|---|---|---|
| GET | `/consultations/{id}/derm` | Dermatology assessment of a consultation |
| PUT | `/consultations/{id}/derm` | Save the dermatology assessment (draft consultations, treating doctor) |
| GET | `/consultations/{id}/photos` | Clinical photographs of a consultation (metadata) |
| POST | `/consultations/{id}/photos` | Add a clinical photograph (consent required; JPEG/PNG/WebP, max 6 MB, encrypted at rest) |
| GET | `/derm/vocabulary` | Dermatology coded vocabulary (problem types, lesions, sites, ...) |
| POST | `/photos/{id}/entered-in-error` | Mark a photograph entered-in-error (never deleted) |
| GET | `/photos/{id}/image` | Image bytes of a clinical photograph (every view is audited) |

### Handwritten notes (v0.3)

See [HANDWRITING.md](HANDWRITING.md). Page bodies may be up to 9 MB. `VERSION_CONFLICT` (409) when `expectedVersion` is stale; `RECORD_LOCKED` (409) after finalize.

| Method | Path | Description |
|---|---|---|
| GET | `/consultations/{id}/handwriting` | Handwritten note pages and transcriptions of a consultation |
| GET | `/consultations/{id}/handwriting/pages/{page}/ink` | Vector ink of one page (decrypted) |
| GET | `/consultations/{id}/handwriting/pages/{page}/image` | Rendered PNG of one page (view is audited) |
| PUT | `/consultations/{id}/handwriting/pages/{page}` | Save one page: vector ink + rendered PNG (treating doctor, draft only) |
| DELETE | `/consultations/{id}/handwriting/pages/{page}` | Remove a page (draft only) |
| POST | `/consultations/{id}/handwriting/analyse` | Analyse handwriting: recognition (if configured) + proposed structured fields; creates a transcription awaiting review |
| POST | `/consultations/{id}/handwriting/extract` | Propose structured fields from (corrected) note text; stores nothing |
| POST | `/handwriting/transcriptions/{tid}/accept` | Doctor accepts the reviewed text and the fields applied to the consultation |
| POST | `/handwriting/transcriptions/{tid}/discard` | Discard a transcription without applying it |

### Decision support

| Method | Path | Description |
|---|---|---|
| POST | `/consultations/{id}/cds/clinical` | Panel A - clinical knowledge: possible conditions, questions, red flags, guideline options |
| POST | `/consultations/{id}/cds/my-patterns` | Panel B - the doctor's own prescribing in comparable previous cases |
| POST | `/consultations/{id}/cds/network` | Panel C - de-identified prescribing patterns across participating ClinicFlow clinics |
| GET | `/consultations/{id}/decisions` | Suggestions shown vs final prescription, safety overrides and outcomes |
| POST | `/consultations/{id}/outcome` | Record a follow-up treatment outcome (RESOLVED / IMPROVED / UNCHANGED / WORSENED) |
| POST | `/consultations/{id}/safety-check` | Safety check of the saved draft prescription (allergy, pregnancy, age, duplicates, interactions) |

### Analytics

| Method | Path | Description |
|---|---|---|
| GET | `/analytics/doctor` | The signed-in doctor's dashboard and own prescribing analytics |
| GET | `/analytics/network` | De-identified network coverage by diagnosis (thresholds applied) |

### Clinical content

| Method | Path | Description |
|---|---|---|
| PUT | `/catalogue/medications/{id}/clinical` | Edit a medicine's clinical information (returns it to UNREVIEWED) |
| GET | `/cds/content` | Clinical decision-support content and medicine safety data with review status |
| POST | `/cds/content/{type}/{id}/review` | Approve, retire or reopen a content item (clinical reviewers only) |

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
| PUT | `/patients/{id}/network-opt-out` | Record the patient's choice about network use of de-identified data |
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
| GET | `/fhir/{type}/{id}` | Export a record as FHIR R4 JSON. type: Patient | Practitioner | Organization | Appointment | Encounter | Invoice | Bundle (consultation document) |
