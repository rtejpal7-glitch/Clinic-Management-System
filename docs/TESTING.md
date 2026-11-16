# Testing

| Suite | Tool | Count | What it proves |
|---|---|---|---|
| Backend unit | C++ (self-contained runner, `ctest`) | 35 | Billing arithmetic, duplicate scoring, appointment state machine, AES-GCM encryption and tamper detection, Argon2id, password policy, ABHA normalization and masking, mock gateway behaviour, FHIR validator; **v0.2:** CDS minimum-information rule, unapproved content never used, ranking with reasons and severity filtering, red flags with incomplete data, medication safety (pregnancy, allergy, age, duplicates, interactions, reviewed vs draft levels), own-pattern threshold, network disclosure controls (min cases/doctors/clinics, dominance, small cells); **v0.3:** note-text extraction (sections, shorthand, durations, negation, query diagnoses, prescription abbreviations, tolerance of recognition errors with correction suggestions, never inventing findings), recognition-provider response parsing, hosted-database URL credentials |
| API integration | pytest + requests against a live server and PostgreSQL | 115 | End-to-end business rules, RBAC and security behaviour, DB constraints under concurrency, ABDM mock flows, FHIR output; **v0.2:** multi-clinic isolation (9), dermatology/CDS/intelligence (19); **v0.3:** handwritten notes (9, including both recognition adapters over TLS against a local test double) |
| Frontend unit | `node --test` | 19 | Client validation mirrors server rules; money parsing and formatting; bill preview matches server math; ink geometry and eraser hit-testing; Docker and Vercel serve the same security headers |
| End-to-end UI | Playwright (Chromium) | 20 + 10 + 9 | The real UI driven as receptionist, doctor and admin (`run-e2e.mjs`), and the dermatology flow (`run-derm-e2e.mjs`: assessment, consented photo, three panels on one row, add/modify suggestion, safety check, finalize, decision record, outcome, insights, content review, clinic network, tablet layout), and handwritten notes on an emulated iPad (`run-handwriting-e2e.mjs`: pen input with pressure, palm rejection, eraser, undo, autosave, pages, review beside the original, corrections, explicit ticks, accept into notes/assessment/diagnoses/draft Rx, decision support, read-only after finalize, portrait) |

All suites were run green on the v0.3 build: unit 35/35, API 115/115 (three consecutive runs), frontend 19/19, E2E 20/20, 10/10 and 9/9, no browser console errors. The dev server sends the production Content-Security-Policy, so the E2E suites run under it.

**Not covered automatically:** real Safari/WebKit on an iPad (the suites use Chromium with iPad emulation), live Google Vision or other recognition services, and the Docker/Vercel deployments themselves. See the manual checklist in [HANDWRITING.md](HANDWRITING.md).

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
python3 scripts/seed_derm_demo.py --approve-content-for-demo   # dermatology suite needs reviewer.demo
E2E_BASE_URL=http://localhost:5173 npm run test:e2e:derm
E2E_BASE_URL=http://localhost:5173 npm run test:e2e:handwriting

# Recognition-provider tests start a second server; point them at the binary and its env file
CF_SERVER_BIN=$PWD/backend/build/clinicflow-server CF_SERVER_ENV_FILE=/path/to/server.env pytest -q tests/api/test_handwriting.py
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

## Dermatology extension: coverage of the brief's test list (section 12)

| Brief item | Tests |
|---|---|
| 1. Derm consultation form | api: test_dermatology::test_assessment_save_validate_and_lock; e2e-derm: structured assessment |
| 2. Symptom analysis, insufficient data | unit: cds_requires_minimum_information, cds_ranks_with_reasons…; api: ::test_panel_a_insufficient_data, ::test_unreviewed_content_is_never_used, ::test_red_flag_for_breathing_difficulty |
| 3. Panel A recommendations with references | api: ::test_unreviewed_content_is_never_used (references, reasoning, no auto-prescribing) |
| 4. Panel B from real history | api: ::test_my_patterns_from_real_history_and_decision_tracking (fresh doctor = insufficient, 3 real cases = OK, other doctor blocked) |
| 5. Panel C aggregation and privacy | api: ::test_network_patterns_thresholds_and_deidentification (withheld below threshold without counts; 12 cases / 4 doctors / 2 clinics shown; no identities; own cases excluded; leaving withdraws), ::test_network_requires_participation, ::test_patient_opt_out_excludes_from_network; unit: patterns_network_disclosure_controls |
| 6. Three panels, one screen | e2e-derm: panels on one row; tablet layout |
| 7. Finalization, accept/modify/reject tracking, audit | api: decision tracking test (MODIFIED / DOCTOR_ADDED / NOT_USED), ::test_allergy_blocks_finalize_until_override; e2e-derm: decisions & outcome |
| 8. Catalogue | api: ::test_editing_medicine_information_resets_review, ::test_review_requires_reviewer_and_notes |
| 9. Doctor dashboard and analytics | api: ::test_doctor_dashboard_analytics, ::test_network_analytics_hides_small_cohorts; e2e-derm: practice insights |
| 10. Security, multi-clinic isolation, photo access | api: test_multiclinic (9), ::test_photo_consent_type_and_audit, ::test_receptionist_has_no_cds_or_photo_access, ::test_other_doctor_cannot_edit_or_use_cds |

Manual database check of row-level security (no clinic context set):

```sql
-- connected as the application role, without app.facility_id
SELECT count(*) FROM patients;          -- 0, although the demo database holds hundreds of patients
```

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

6. (v0.2) A CSS class name clash made all new percentage bars render 36 px wide. Found from E2E screenshots; renamed.
7. (v0.2) Decisions for suggestions that were not used had no medicine name. Found from E2E screenshots; the name is now resolved from the catalogue.

## Not yet covered

Cross-browser runs (Firefox, WebKit), load and performance testing, accessibility audit with assistive technology, and FHIR validation with the official HL7 validator and NRCeS IG.
