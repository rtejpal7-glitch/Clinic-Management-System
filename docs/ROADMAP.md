# Known limitations and roadmap

## Known limitations of the PoC

**Regulatory and certification**
- Not NABH-certified or ABDM-certified. The traceability matrix (v0.2) shows 22 of 51 mapped objective elements tested, 1 implemented, 18 partial and 10 not started. Several Core elements are partial.
- ABDM runs on a mock adapter only. No live HFR/HPR registration, no gateway callbacks, no data-flow encryption.
- FHIR output is checked structurally, not against the official NRCeS IG. ABHA identifier systems are placeholders.

**Clinical**
- Dermatology decision support (v0.2) uses an AI-drafted starter knowledge base that **must be clinically reviewed** before use. Its rule weights are not validated for diagnostic accuracy. Safety checks cover only the 53 coded starter medicines and 10 interaction pairs; there are no dose-range checks. A licensed drug database is needed.
- Prescribing patterns become meaningful only with real finalized cases. Demo data is synthetic and created in a single month.
- Dermatology findings and photographs are not yet in the FHIR bundle (Observation/Media). Handwritten pages are not in it either.
- Handwritten notes (v0.3): recognition depends on an external or self-hosted provider; none was tested live. Extraction is rule-based, English-only and dermatology-focused. The writing pad was tested in Chromium with iPad emulation, not in Safari on a real iPad. Lasso/move, highlighter and typed text boxes on the page are not implemented.
- Starter catalogues (40 medicines, 17 investigations, 20 ICD-10 codes) need replacing with a licensed formulary and terminology.
- No nurse/triage role for pre-consultation vitals. No referrals, order sets, digital signatures or multilingual summaries.
- No lab or radiology result capture (optional LTS/RLS modules), and no daycare module.

**Operational**
- Several clinics per installation (v0.2), but one location per clinic and one specialty per clinic. Patient numbers and some settings (ID prefixes, idle timeout, duplicate rules) are installation-wide. Doctor leave and blocked dates are not modelled.
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
- Clinical review of the dermatology starter content; validation of Panel A against labelled cases; regulatory (software as a medical device) assessment
- Licensed drug database (CDSCO product information, interactions, dose ranges) behind the catalogue; medication-error register (MOM.2c)
- FHIR mapping of dermatology assessment and photographs; teledermatology image exchange
- Order sets from each doctor's frequent prescriptions (COP.1e); restricted-medicine authorization (COP.1f); duplicate investigation-order alerts
- Further specialty modules via `ConsultationExtension`
- Nurse triage role; digital signatures (COP.1j); multilingual prescriptions (COP.1k)
- Optional LTS (lab) and RLS (radiology) modules with LOINC/DICOM

### Phase D — product and scale
- Patient portal / WhatsApp channel (DAC.1d), mobile app for doctors (DAC.1e)
- Multi-clinic tenancy with row-level security; reporting and NABH clinic KPIs (IMS.2)
- CI pipeline: build, unit, API, E2E (Chromium, Firefox, WebKit), dependency and container scanning
- ISO 27001 controls and WASA certification (IMS.3)
