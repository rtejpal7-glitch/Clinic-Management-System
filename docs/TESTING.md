# Testing

| Suite | Tool | Count | What it proves |
|---|---|---|---|
| Backend unit | C++ (self-contained runner, `ctest`) | 19 | Billing arithmetic, duplicate scoring, appointment state machine, AES-GCM encryption and tamper detection, Argon2id, password policy, ABHA normalization and masking, mock gateway behaviour, FHIR validator |
| API integration | pytest + requests against a live server and PostgreSQL | 78 | End-to-end business rules, RBAC and security behaviour, DB constraints under concurrency, ABDM mock flows, FHIR output |
| Frontend unit | `node --test` | 11 | Client validation mirrors server rules; money parsing and formatting; bill preview matches server math |
| End-to-end UI | Playwright (Chromium) | 20 | The real UI driven as receptionist, doctor and admin, with a screenshot of every screen |

All suites were run green on the delivered build: 19/19, 78/78 (three consecutive runs), 11/11 and 20/20.

## Running

```bash
# 1. Backend unit tests (no database)
cd backend && cmake --build build && ctest --test-dir build --output-on-failure

# 2. API integration tests — needs a running server (any database; tests create their own data)
pip install pytest requests
CF_API_URL=http://localhost:8000/api/v1 CF_ADMIN_PASSWORD='<admin password>' pytest -q tests/api

# 3. Frontend unit tests
cd frontend && npm test

# 4. End-to-end — needs server + demo seed + dev server (or the Docker stack on :8080)
python3 scripts/seed_demo.py
cd frontend && npm run dev &            # http://localhost:5173
npx playwright install chromium         # first time only
E2E_BASE_URL=http://localhost:5173 npm run test:e2e   # screenshots in frontend/e2e/screenshots/
```

Tip: run the API tests against a separate database from your demo data. They create many synthetic users and patients.

## Coverage against the brief's test list

| Brief item | Tests |
|---|---|
| Successful registration / unique ID / duplicate detection / search | test_patients: register_patient_generates_unique_id, duplicate_detection_blocks_then_allows_with_reason, family_sharing_mobile_is_not_flagged, search_by_number_name_and_mobile |
| Unauthorized modification attempts | test_patients: unauthorized_modification_attempt; test_security: receptionist_/doctor_/admin_ RBAC tests |
| Booking / double-booking / cancellation / follow-up / queue token | test_appointments: book_appointment, double_booking_is_prevented, concurrent_double_booking_only_one_wins, cancel_and_reschedule, follow_up_scheduling, walk_in_issues_queue_token, queue_tokens_increment, check_in_today |
| Consultation / draft & finalized / prescription / investigation orders / unauthorized access | test_clinical: consultation_creation_and_draft_save, finalized_record_is_locked_and_addenda_work, prescription_and_investigation_orders, other_doctor_cannot_edit_or_start, receptionist_cannot_access_consultation, stale_version_conflict |
| Invoice / payments / partial payments / calculations | test_billing: invoice_generation_and_calculation, partial_then_full_payment, refund_requires_permission_and_reason, cancel_only_unpaid; unit billing_* |
| Invalid logins / RBAC / unauthorized API / audit / sensitive access | test_security: invalid_login…, account_lockout…, unauthenticated_requests_are_rejected, denied_access_is_audited, login_events_are_audited, audit_log_hash_chain_verifies, patient_view_is_access_logged, dashboard_hides_financials_without_permission |
| Mock ABHA linking / consent creation & withdrawal / mock success & failure / FHIR validation | test_abdm: mock_abha_linking_flow, wrong_otp_and_timeout_fail, consent_creation_denial_and_withdrawal, consent_request_gateway_failure_and_retry, mock_share_success_and_failure_with_retry, fhir_bundle_structure_and_validation |

## Defects found by testing (and fixed)

1. A `Row` outlived its query result (use-after-free crash). Fixed by giving rows shared ownership of results.
2. A range-for over a temporary container (crash in ABDM retry). Fixed, and the codebase was scanned for the same pattern.
3. Query parameters were not URL-decoded by Oat++ (searches with spaces failed). Fixed with central decoding.
4. A parameter type was inferred as `text` against a `uuid` column. Fixed with explicit casts.
5. **Keep-alive request corruption:** bodies sent to body-less endpoints poisoned the next request on the connection. Found only by the browser E2E tests. Fixed with a body-capture interceptor and covered by a regression test.

## Database-level checks performed manually

```sql
-- ABHA values are ciphertext at rest
SELECT value_ciphertext FROM patient_identifiers LIMIT 1;      -- v1:... (no plaintext)
-- Runtime role cannot tamper with audit or payments
SET ROLE clinicflow_app; UPDATE audit_logs SET action='x';     -- ERROR: permission denied
DELETE FROM payments;                                          -- ERROR: permission denied
-- Reminders queued for future bookings
SELECT status, count(*) FROM notifications GROUP BY 1;
```

## Not yet covered

Cross-browser runs (Firefox, WebKit), load and performance testing, accessibility audit with assistive technology, and FHIR validation with the official HL7 validator and NRCeS IG.
