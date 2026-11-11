# Security implementation

This document lists the security controls in the PoC, where they live in the code, and what remains for production. ClinicFlow processes personal health information, so it is designed around confidentiality, integrity and traceability from the start.

## Authentication

| Control | Implementation |
|---|---|
| Password storage | Argon2id (libargon2 reference implementation), m = 19 MiB, t = 2, p = 1 (OWASP baseline), 16-byte random salt, encoded hash. `security/Crypto.cpp` |
| Password policy | At least 10 characters, letters and digits, must not contain the username, maximum 128. Enforced server-side (`AuthService::validatePasswordPolicy`) and mirrored client-side. |
| Temporary passwords | New and reset accounts get `must_change_password`. Every API call except change-password, me and logout returns `403 PASSWORD_CHANGE_REQUIRED` until it's changed. |
| Brute-force protection | 5 failed attempts lock the account for 15 minutes. Failures are audited. Unknown usernames get a dummy Argon2 verification so response time doesn't reveal which accounts exist, and the error message is identical. |
| Sessions | 256-bit random opaque bearer tokens. Only SHA-256(token) is stored. Absolute lifetime (default 12 h), sliding idle timeout (clinic setting, default 30 min), revoked on logout, password change, password reset and user deactivation. |
| Client idle lock | The SPA signs out after the same idle period. The token lives in `sessionStorage` (cleared when the tab closes). |

**Token storage trade-off:** `sessionStorage` is readable by script, so an XSS flaw could steal a token. Mitigations: React escaping, no `dangerouslySetInnerHTML`, a strict CSP (`script-src 'self'`) at nginx, short idle timeouts and server-side revocation. Moving to `HttpOnly; Secure; SameSite=Strict` cookies with CSRF protection is on the roadmap.

## Authorization (RBAC)

- 36 permissions grouped by module (`V007`, `V009`, `V010`). System roles ADMIN, DOCTOR, RECEPTIONIST, CLINICAL_REVIEWER (approves decision-support content; no patient access) and PLATFORM_ADMIN (creates clinics, edits shared system roles). Custom roles can be created and mapped in the UI.
- **Deny by default:** every protected endpoint calls `auth(request, <permission>)` and the backend rejects anything not explicitly granted. Hiding buttons in the UI is a convenience only.
- **Least privilege by role:** administrators have no clinical permissions. Receptionists can't read clinical notes, allergies or audit logs. Doctors can't see billing or financial dashboard figures. Refunds and cancellations need a separate `billing.refund` permission.
- **Ownership rules:** only the treating doctor can edit or finalize their draft. Other doctors can read for continuity of care.
- **Lock-out protection:** the system refuses role or user changes that would leave no active user with `user.manage`.
- Every denial is written to the audit log (`ACCESS_DENIED`).
- **Clinic isolation (v0.2):** PostgreSQL row-level security on every patient, clinical, financial and ABDM table. The clinic is taken from the authenticated user, never from the request. Users, roles and audit entries are filtered by clinic in code. Clinic administrators cannot grant the platform role or change shared system roles. A database session with no clinic context sees zero patient rows (verified manually, see TESTING.md).
- **Decision support:** panels are available only to the treating doctor (`cds.use`; network also needs `network.read`). Content approval needs `cds.review`. Photographs need `photo.read` / `photo.write`.

## Audit logging

- Business actions are audited **in the same transaction** as the change.
- Read access to sensitive records is logged: patient profile view, clinical history view, consultation view, FHIR export, and search (query type and result count, not the raw search term).
- Each entry records user, roles, action, entity, patient, outcome (SUCCESS / FAILURE / DENIED), IP, user-agent, JSON details and timestamp.
- **Append-only:** a trigger rejects UPDATE, DELETE and TRUNCATE, and UPDATE/DELETE are revoked from the application role.
- **Tamper evidence:** each entry stores `hash = SHA-256(prev_hash | timestamp | user | action | entity | outcome | details)`, computed in SQL. `GET /audit-logs/verify` recomputes the chain and reports the first broken entry. This detects edits made by someone with direct database superuser access.
- Patient demographic changes store field-level before/after values in `patient_change_history`. The audit log records only the field names, to avoid copying personal data into the log.

## Data protection

| Area | Implementation | Production requirement |
|---|---|---|
| In transit | The API is designed to sit behind TLS termination (nginx / load balancer). The PoC runs HTTP on localhost only. | TLS 1.2+ with valid certificates, HSTS, HTTP→HTTPS redirect, TLS between proxy and API on untrusted networks, `sslmode=verify-full` to PostgreSQL. |
| Field-level encryption at rest | ABHA numbers and addresses: AES-256-GCM, random 96-bit nonce, authentication tag, versioned `v1:` prefix. Equality search uses an HMAC-SHA256 blind index with a separate key. | Keys in a KMS or HSM (AWS KMS, Azure Key Vault, HashiCorp Vault), not environment files. Key-rotation job (re-encrypt to `v2:`). |
| Database at rest | — | Encrypted volume or managed-database storage encryption (for example RDS/Cloud SQL encryption, LUKS). |
| Backups | — | Encrypted `pg_dump`/WAL archives (for example `pgBackRest` with repository encryption), stored off-site, restore tested regularly. |
| Caching | `Cache-Control: no-store` on all API responses. `index.html` not cached. | |
| Secrets | Only from the environment. `.env` is git-ignored. `generate-env.sh` creates random values with mode 600. | Secret manager, rotation. |
| Least privilege in DB | API connects as `clinicflow_app` (no DDL, no UPDATE/DELETE on audit, payments or addenda). Migrations use `clinicflow_owner`. | Separate credentials per environment. |
| Logs | Application logs avoid PHI (the notification log masks phone numbers). | Central log collection with access control. |

## Application security

- **SQL injection:** every query uses `PQexecParams` with bound parameters. Dynamic SQL is limited to whitelisted column fragments built in code. A test confirms that injection strings in search return nothing.
- **Input validation:** server-side validation for every write (formats, ranges, enumerations, lengths), plus database CHECK constraints. Validation errors return field-level messages.
- **Mass assignment:** request DTOs whitelist fields. Unknown JSON keys are ignored, never bound.
- **Error handling:** uniform error envelope. Unexpected exceptions return a generic 500 and are logged server-side without stack traces in responses.
- **Request limits:** request bodies above 1 MB are rejected (413). Query parameters are capped at 200 characters. The database `statement_timeout` is 15 s.
- **Concurrency:** optimistic locking on patients, appointments and consultations (`version`). `SELECT … FOR UPDATE` on bills during payments. An advisory lock serializes registrations during duplicate checks. The exclusion constraint makes double-booking impossible even under concurrent requests (tested with 4 simultaneous bookings).
- **HTTP headers:** API adds `X-Content-Type-Options`, `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`, `Cache-Control: no-store`. nginx adds a CSP and Permissions-Policy.
- **Keep-alive safety:** every request body is fully consumed before routing (see ARCHITECTURE.md, decision 8).

## Privacy and consent (DPDP Act 2023 and ABDM)

- ABHA linking requires explicit confirmation that the patient agreed. The confirmation is recorded in the audit details.
- ABDM consent records hold purpose, health-information types, date range, expiry and status. Expiry is enforced by the background worker, and revocation by staff is supported. Sharing is refused without an active, matching consent.
- **Clinical photographs:** consent checkbox and scope are required and stored; images are encrypted (AES-256-GCM), checked by content type (magic bytes) and size, served with `no-store`, every view audited, never deleted.
- **Network (secondary use):** off by default. A clinic joins only with a recorded agreement reference. Records are de-identified at capture: no direct identifiers or free text, keyed pseudonyms, month-level dates, coarse age bands. Patients can opt out, which withdraws their contributed records. A clinic that leaves has its records withdrawn. Aggregates are suppressed below 10 cases, 3 doctors or 2 clinics, or when one clinic contributes over 70% of cases; small cells are grouped. These are technical controls; the clinic still needs its own legal basis, notice and agreement under the DPDP Act, 2023 and its rules.
- A clinic deploying ClinicFlow is the data fiduciary. It needs a privacy notice, a grievance contact and a documented process for data-principal requests. The PoC doesn't implement a patient-facing portal for these.

## Not yet implemented (production checklist)

Two-factor authentication · HttpOnly cookie sessions with CSRF tokens · rate limiting at the edge · configurable password expiry and history · audit log export and retention configuration · KMS-backed key management and rotation · TLS everywhere (exercised in deployment) · encrypted backups · security monitoring and alerting · dependency and container vulnerability scanning in CI · external penetration test / WASA certification (NABH CMS IMS.3b) · ISO 27001 controls (IMS.3a).
