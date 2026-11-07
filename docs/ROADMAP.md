# Known limitations and roadmap

## Known limitations of the PoC

**Regulatory and certification**
- Not NABH-certified or ABDM-certified. The traceability matrix shows 17 of 43 mapped objective elements tested, 15 partial and 11 not started. Several Core elements are partial.
- ABDM runs on a mock adapter only. No live HFR/HPR registration, no gateway callbacks, no data-flow encryption.
- FHIR output is checked structurally, not against the official NRCeS IG. ABHA identifier systems are placeholders.

**Clinical**
- No clinical decision support (drug–drug interactions, dose checks, allergy cross-reactivity). Only a simple text match against recorded allergies is shown, and it is labelled as such.
- Starter catalogues (40 medicines, 17 investigations, 20 ICD-10 codes) need replacing with a licensed formulary and terminology.
- No nurse/triage role for pre-consultation vitals. No referrals, order sets, digital signatures or multilingual summaries.
- No lab or radiology result capture (optional LTS/RLS modules), and no daycare module.

**Operational**
- Single clinic and location. Doctor leave and blocked dates are not modelled.
- Reminders are written to an outbox and "sent" only to a mock log channel. Mobile OTP verification isn't implemented.
- Payments are recorded, not processed (no gateway or UPI QR). No insurance (FPM.4).
- No audit export, configurable retention, reports beyond the daily billing summary, or data-migration tooling.

**Security** (see SECURITY.md for the production checklist)
- No 2FA. Token in sessionStorage rather than HttpOnly cookies. TLS, KMS-backed keys, encrypted backups and edge rate limiting are deployment work.
- Docker images were written but not built in the development environment. The native build and all tests were verified.

## Roadmap

### Phase A — close Core gaps (towards NABH CMS Basic level, CPD scope)
- Mobile OTP verification via an SMS provider with DLT templates (AAC.1b)
- Registration source / point of origin (AAC.1c); configurable mandatory fields and national-ID capture (AAC.1a); more patient-ID formats (AAC.1e)
- Referrals in the visit summary; department field (COP.1i)
- Invoice template configuration with service dates and disclaimers (FPM.3b); payment-gateway / UPI integration (FPM.3d)
- Full password policy: expiry, history, special characters, common-password list (DOM.3b); 2FA
- Production encryption at rest and in backups (DAC.2a)
- Audit rule and retention configuration and export (DAC.2c)

### Phase B — ABDM live integration
- HFR/HPR onboarding; `LiveAbdmGateway` with sandbox credentials; callback endpoints; care-context discovery and linking; HI data-flow encryption
- DiagnosticReport and Immunization records; drug coding (SNOMED CT / NRCeS drug registry); official FHIR validation in CI
- NHA M1–M3 certification

### Phase C — clinical depth
- Separately validated CDS service behind the `ClinicalDecisionSupport` interface
- Order sets and favourites; restricted-medicine authorization (COP.1e, COP.1f); duplicate-order alerts (COP.1g)
- Nurse triage role; digital signatures (COP.1j); multilingual prescriptions (COP.1k)
- Optional LTS (lab) and RLS (radiology) modules with LOINC/DICOM

### Phase D — product and scale
- Patient portal / WhatsApp channel (DAC.1d), mobile app for doctors (DAC.1e)
- Multi-clinic tenancy with row-level security; reporting and NABH clinic KPIs (IMS.2)
- CI pipeline: build, unit, API, E2E (Chromium, Firefox, WebKit), dependency and container scanning
- ISO 27001 controls and WASA certification (IMS.3)
