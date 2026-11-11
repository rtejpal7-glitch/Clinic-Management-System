-- ClinicFlow V010: dermatology consultation, clinical photographs, reviewable
-- clinical-decision-support (CDS) content, medication safety data, and the
-- data needed for the doctor's own and the cross-clinic prescribing patterns.
--
-- Principles enforced in the schema:
--  * Clinical knowledge (conditions, red flags, medicine safety data, interactions)
--    is global reference content with a review workflow. Nothing UNREVIEWED is used
--    as decision support; the API only reads APPROVED rows for recommendations.
--  * Patient-level data stays in RLS-protected, facility-scoped tables.
--  * The cross-clinic network table holds no patient identifiers, no free text,
--    no facility or doctor identifiers (only keyed pseudonyms) and month-level dates.

-- ---------------------------------------------------------------------------
-- Permissions and the clinical reviewer role
-- ---------------------------------------------------------------------------
INSERT INTO permissions (code, module, description) VALUES
  ('photo.read',     'clinical',  'View clinical photographs'),
  ('photo.write',    'clinical',  'Capture clinical photographs (with patient consent)'),
  ('cds.use',        'clinical',  'Use clinical decision support and prescribing pattern panels'),
  ('cds.review',     'clinical',  'Review and approve clinical decision-support content and medicine safety data'),
  ('insights.read',  'clinical',  'View own consultation and prescribing analytics'),
  ('network.read',   'clinical',  'View de-identified ClinicFlow network prescribing patterns');

INSERT INTO roles (code, name, description, is_system) VALUES
  ('CLINICAL_REVIEWER', 'Clinical content reviewer',
   'Qualified clinician who reviews and approves decision-support content and medicine safety data', true);

INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM roles r JOIN permissions p ON p.code = ANY (CASE r.code
  WHEN 'DOCTOR' THEN ARRAY['photo.read','photo.write','cds.use','insights.read','network.read']
  WHEN 'CLINICAL_REVIEWER' THEN ARRAY['cds.review','catalogue.read','dashboard.read']
  END)
WHERE r.code IN ('DOCTOR','CLINICAL_REVIEWER') AND r.facility_id IS NULL;

-- ---------------------------------------------------------------------------
-- Specialty of a consultation (extensibility: other specialties add their own
-- assessment tables and register a module keyed by this code)
-- ---------------------------------------------------------------------------
ALTER TABLE consultations ADD COLUMN specialty_code text NOT NULL DEFAULT 'GENERAL'
  CHECK (specialty_code IN ('GENERAL','DERMATOLOGY'));

-- Patient choice about secondary use of their de-identified data in the network
ALTER TABLE patients ADD COLUMN network_opt_out boolean NOT NULL DEFAULT false;
ALTER TABLE patients ADD COLUMN network_opt_out_at timestamptz;

-- ---------------------------------------------------------------------------
-- Dermatology assessment (1:1 with a consultation; locked when finalized)
-- Coded values are validated by the API against the vocabulary in
-- backend/src/derm/DermVocabulary.hpp (mirrored in the frontend).
-- ---------------------------------------------------------------------------
CREATE TABLE derm_assessments (
  id                         uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id            uuid NOT NULL UNIQUE REFERENCES consultations(id) ON DELETE RESTRICT,
  patient_id                 uuid NOT NULL REFERENCES patients(id),
  facility_id                uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  problem_type               text,
  duration_value             integer CHECK (duration_value IS NULL OR duration_value BETWEEN 0 AND 1000),
  duration_unit              text CHECK (duration_unit IS NULL OR duration_unit IN ('DAYS','WEEKS','MONTHS','YEARS')),
  severity                   text CHECK (severity IS NULL OR severity IN ('MILD','MODERATE','SEVERE')),
  symptoms                   text[] NOT NULL DEFAULT '{}',
  spreading                  boolean,
  recurring                  boolean,
  previous_episodes          integer CHECK (previous_episodes IS NULL OR previous_episodes BETWEEN 0 AND 100),
  aggravating_factors        text[] NOT NULL DEFAULT '{}',
  relieving_factors          text[] NOT NULL DEFAULT '{}',
  body_sites                 text[] NOT NULL DEFAULT '{}',
  lesion_types               text[] NOT NULL DEFAULT '{}',
  colours                    text[] NOT NULL DEFAULT '{}',
  configuration              text,
  distribution               text,
  extent_bsa_percent         numeric(4,1) CHECK (extent_bsa_percent IS NULL OR extent_bsa_percent BETWEEN 0 AND 100),
  findings_notes             text,
  other_conditions           text,
  previous_derm_treatments   text,
  previous_treatment_response text CHECK (previous_treatment_response IS NULL OR
                               previous_treatment_response IN ('GOOD','PARTIAL','NONE','WORSENED','NOT_TRIED')),
  family_history             text,
  pregnancy_status           text CHECK (pregnancy_status IS NULL OR
                               pregnancy_status IN ('NOT_APPLICABLE','NOT_PREGNANT','PREGNANT','BREASTFEEDING','UNKNOWN')),
  updated_at                 timestamptz NOT NULL DEFAULT now(),
  updated_by                 uuid REFERENCES users(id)
);
CREATE TRIGGER trg_derm_lock BEFORE INSERT OR UPDATE OR DELETE ON derm_assessments
  FOR EACH ROW EXECUTE FUNCTION cf_consultation_child_lock();

-- ---------------------------------------------------------------------------
-- Clinical photographs. Image bytes are encrypted at the application layer
-- (AES-256-GCM, same key management as other sensitive fields). Never deleted;
-- mistaken photos are marked ENTERED_IN_ERROR and hidden from normal views.
-- ---------------------------------------------------------------------------
CREATE TABLE clinical_photos (
  id                 uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id    uuid NOT NULL REFERENCES consultations(id) ON DELETE RESTRICT,
  patient_id         uuid NOT NULL REFERENCES patients(id),
  facility_id        uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  body_site          text,
  caption            text CHECK (caption IS NULL OR length(caption) <= 200),
  content_type       text NOT NULL CHECK (content_type IN ('image/jpeg','image/png','image/webp')),
  size_bytes         integer NOT NULL CHECK (size_bytes > 0 AND size_bytes <= 6291456),
  sha256             text NOT NULL,
  image_enc          text NOT NULL,
  consent_obtained   boolean NOT NULL CHECK (consent_obtained),
  consent_scope      text NOT NULL CHECK (consent_scope IN ('CLINICAL_RECORD_ONLY','CLINICAL_RECORD_AND_TEACHING')),
  status             text NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE','ENTERED_IN_ERROR')),
  status_reason      text,
  captured_by        uuid NOT NULL REFERENCES users(id),
  captured_at        timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_photos_consultation ON clinical_photos(consultation_id);
CREATE INDEX ix_photos_patient ON clinical_photos(patient_id, captured_at DESC);
CREATE OR REPLACE FUNCTION cf_photo_guard() RETURNS trigger AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    RAISE EXCEPTION 'clinical photographs cannot be deleted; mark them entered-in-error' USING ERRCODE = 'P0001';
  END IF;
  IF NEW.image_enc <> OLD.image_enc OR NEW.consultation_id <> OLD.consultation_id OR NEW.patient_id <> OLD.patient_id
     OR NEW.captured_at <> OLD.captured_at OR NEW.captured_by <> OLD.captured_by THEN
    RAISE EXCEPTION 'clinical photographs are immutable' USING ERRCODE = 'P0001';
  END IF;
  RETURN NEW;
END $$ LANGUAGE plpgsql;
CREATE TRIGGER trg_photo_guard BEFORE UPDATE OR DELETE ON clinical_photos
  FOR EACH ROW EXECUTE FUNCTION cf_photo_guard();

-- ---------------------------------------------------------------------------
-- Medicine catalogue: dermatology / safety fields + review workflow
-- ---------------------------------------------------------------------------
ALTER TABLE medication_catalogue
  ADD COLUMN generic_code           text CHECK (generic_code IS NULL OR generic_code ~ '^[A-Z0-9_]{2,60}$'),
  ADD COLUMN ingredient_codes       text[] NOT NULL DEFAULT '{}',
  ADD COLUMN category               text,
  ADD COLUMN specialty              text,
  ADD COLUMN routes                 text[] NOT NULL DEFAULT '{}',
  ADD COLUMN available_strengths    text,
  ADD COLUMN default_dose           text,
  ADD COLUMN default_frequency      text,
  ADD COLUMN default_duration_days  integer CHECK (default_duration_days IS NULL OR default_duration_days BETWEEN 1 AND 365),
  ADD COLUMN common_dosage          text,
  ADD COLUMN warnings               text,
  ADD COLUMN contraindication_rules jsonb NOT NULL DEFAULT '[]',   -- [{"tag":"PREGNANCY","level":"CONTRAINDICATED|CAUTION","note":"..."}]
  ADD COLUMN contraindications_text text,
  ADD COLUMN interactions_text      text,
  ADD COLUMN high_risk              boolean NOT NULL DEFAULT false, -- high-alert / high-risk medicine (MOM)
  ADD COLUMN aware_category         text CHECK (aware_category IS NULL OR aware_category IN ('ACCESS','WATCH','RESERVE')),
  ADD COLUMN restricted_antimicrobial boolean NOT NULL DEFAULT false,
  ADD COLUMN reference              text,
  ADD COLUMN review_status          text NOT NULL DEFAULT 'UNREVIEWED' CHECK (review_status IN ('UNREVIEWED','APPROVED','RETIRED')),
  ADD COLUMN reviewed_by            uuid REFERENCES users(id),
  ADD COLUMN reviewed_at            timestamptz,
  ADD COLUMN review_notes           text,
  ADD COLUMN content_source         text NOT NULL DEFAULT 'CLINIC' CHECK (content_source IN ('CLINIC','STARTER_DRAFT'));
-- Brands are distinguished from the generic product: brand rows carry brand_name;
-- generic rows have brand_name NULL. All products of one molecule share generic_code.
CREATE INDEX ix_medcat_generic ON medication_catalogue(generic_code);
-- Content that is used for decision support must say where it came from.
ALTER TABLE medication_catalogue ADD CONSTRAINT ck_medcat_approved_has_reference
  CHECK (review_status <> 'APPROVED' OR reference IS NOT NULL);

CREATE TABLE medication_interactions (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  generic_a       text NOT NULL,
  generic_b       text NOT NULL,
  severity        text NOT NULL CHECK (severity IN ('CONTRAINDICATED','MAJOR','MODERATE')),
  description     text NOT NULL,
  reference       text NOT NULL,
  review_status   text NOT NULL DEFAULT 'UNREVIEWED' CHECK (review_status IN ('UNREVIEWED','APPROVED','RETIRED')),
  reviewed_by     uuid REFERENCES users(id),
  reviewed_at     timestamptz,
  review_notes    text,
  created_at      timestamptz NOT NULL DEFAULT now(),
  CHECK (generic_a < generic_b),
  UNIQUE (generic_a, generic_b)
);

-- ---------------------------------------------------------------------------
-- CDS knowledge base (global, versioned, reviewable)
-- ---------------------------------------------------------------------------
CREATE TABLE cds_conditions (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code            text NOT NULL UNIQUE CHECK (code ~ '^[A-Z0-9_]{2,40}$'),
  name            text NOT NULL,
  icd10_code      text,
  specialty       text NOT NULL DEFAULT 'DERMATOLOGY',
  summary         text,
  refs            jsonb NOT NULL DEFAULT '[]',   -- [{"citation":"...", "url":"..."}]
  content_version integer NOT NULL DEFAULT 1,
  content_source  text NOT NULL DEFAULT 'CLINIC' CHECK (content_source IN ('CLINIC','STARTER_DRAFT')),
  review_status   text NOT NULL DEFAULT 'UNREVIEWED' CHECK (review_status IN ('UNREVIEWED','APPROVED','RETIRED')),
  reviewed_by     uuid REFERENCES users(id),
  reviewed_at     timestamptz,
  review_notes    text,
  created_at      timestamptz NOT NULL DEFAULT now(),
  updated_at      timestamptz NOT NULL DEFAULT now()
);
CREATE TRIGGER trg_cds_cond_touch BEFORE UPDATE ON cds_conditions FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

-- Weighted findings that support a condition (the matching model is a transparent,
-- additive score; see backend/src/derm/CdsEngine.cpp)
CREATE TABLE cds_condition_features (
  id            uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  condition_id  uuid NOT NULL REFERENCES cds_conditions(id) ON DELETE CASCADE,
  feature       text NOT NULL,   -- token, e.g. LESION:WHEAL, SITE:FACE, SYMPTOM:ITCHING
  weight        smallint NOT NULL CHECK (weight BETWEEN 1 AND 5),
  rationale     text
);
CREATE INDEX ix_cds_features ON cds_condition_features(condition_id);

CREATE TABLE cds_condition_content (
  id            uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  condition_id  uuid NOT NULL REFERENCES cds_conditions(id) ON DELETE CASCADE,
  kind          text NOT NULL CHECK (kind IN ('OBSERVATION','QUESTION','TREATMENT','REFERRAL')),
  text          text NOT NULL,
  generic_codes text[] NOT NULL DEFAULT '{}',  -- medicines this treatment option refers to
  line          text CHECK (line IS NULL OR line IN ('FIRST','SECOND','ADJUNCT')),
  severities    text[] NOT NULL DEFAULT '{}',  -- empty = any severity
  reference     text,
  sort_order    integer NOT NULL DEFAULT 0
);
CREATE INDEX ix_cds_content ON cds_condition_content(condition_id, kind, sort_order);

CREATE TABLE cds_red_flags (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code            text NOT NULL UNIQUE,
  title           text NOT NULL,
  criteria        jsonb NOT NULL,     -- {"all":[tokens], "any":[tokens]}
  guidance        text NOT NULL,
  reference       text,
  content_source  text NOT NULL DEFAULT 'CLINIC' CHECK (content_source IN ('CLINIC','STARTER_DRAFT')),
  review_status   text NOT NULL DEFAULT 'UNREVIEWED' CHECK (review_status IN ('UNREVIEWED','APPROVED','RETIRED')),
  reviewed_by     uuid REFERENCES users(id),
  reviewed_at     timestamptz,
  review_notes    text
);

-- Each CDS analysis is stored with the exact input and content version it used.
CREATE TABLE cds_analysis_runs (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id),
  facility_id      uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  engine_version   text NOT NULL,
  status           text NOT NULL CHECK (status IN ('OK','INSUFFICIENT_DATA','NO_APPROVED_CONTENT')),
  input            jsonb NOT NULL,
  result           jsonb NOT NULL,
  created_by       uuid NOT NULL REFERENCES users(id),
  created_at       timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_cds_runs ON cds_analysis_runs(consultation_id, created_at DESC);

-- ---------------------------------------------------------------------------
-- What the doctor was shown, and what the doctor decided
-- ---------------------------------------------------------------------------
CREATE TABLE suggestion_snapshots (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id),
  facility_id      uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  source           text NOT NULL CHECK (source IN ('CLINICAL','MY_PATTERNS','NETWORK')),
  status           text NOT NULL,
  payload          jsonb NOT NULL,
  suggested_generics text[] NOT NULL DEFAULT '{}',
  created_by       uuid NOT NULL REFERENCES users(id),
  created_at       timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_snapshots_consultation ON suggestion_snapshots(consultation_id, source, created_at DESC);

ALTER TABLE prescription_items
  ADD COLUMN generic_code            text,
  ADD COLUMN source_kind             text NOT NULL DEFAULT 'MANUAL'
    CHECK (source_kind IN ('MANUAL','CLINICAL','MY_PATTERNS','NETWORK')),
  ADD COLUMN source_snapshot_id      uuid REFERENCES suggestion_snapshots(id),
  ADD COLUMN suggested_dose          text,
  ADD COLUMN suggested_frequency     text,
  ADD COLUMN suggested_duration_days integer;

CREATE TABLE suggestion_decisions (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id),
  facility_id      uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  snapshot_id      uuid REFERENCES suggestion_snapshots(id),
  source           text NOT NULL CHECK (source IN ('CLINICAL','MY_PATTERNS','NETWORK','DOCTOR')),
  generic_code     text,
  medicine_name    text,
  decision         text NOT NULL CHECK (decision IN ('ACCEPTED','MODIFIED','NOT_USED','DOCTOR_ADDED')),
  suggested        jsonb,
  final_item       jsonb,
  decided_by       uuid NOT NULL REFERENCES users(id),
  decided_at       timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_decisions_consultation ON suggestion_decisions(consultation_id);

-- Safety alerts the prescriber acknowledged/overrode at finalization
CREATE TABLE safety_overrides (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id),
  facility_id      uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  alerts           jsonb NOT NULL,
  reason           text NOT NULL CHECK (length(btrim(reason)) >= 5),
  overridden_by    uuid NOT NULL REFERENCES users(id),
  overridden_at    timestamptz NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------------
-- Prescribing cases: one structured, coded row per finalized dermatology
-- consultation. Source for the doctor's own patterns (Panel B).
-- ---------------------------------------------------------------------------
CREATE TABLE prescribing_cases (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL UNIQUE REFERENCES consultations(id),
  facility_id      uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  doctor_id        uuid NOT NULL REFERENCES doctors(id),
  patient_id       uuid NOT NULL REFERENCES patients(id),
  finalized_at     timestamptz NOT NULL,
  dx_code          text,           -- primary ICD-10 code (NULL if free text only)
  dx_display       text,
  problem_type     text,
  severity         text,
  age_band         text NOT NULL,
  sex              text,
  pregnancy_status text,
  duration_band    text,
  recurring        boolean,
  body_sites       text[] NOT NULL DEFAULT '{}',
  lesion_types     text[] NOT NULL DEFAULT '{}',
  generic_codes    text[] NOT NULL DEFAULT '{}',
  items            jsonb NOT NULL DEFAULT '[]',
  outcome          text CHECK (outcome IS NULL OR outcome IN ('RESOLVED','IMPROVED','UNCHANGED','WORSENED')),
  outcome_at       timestamptz,
  network_shared   boolean NOT NULL DEFAULT false
);
CREATE INDEX ix_cases_doctor_dx ON prescribing_cases(doctor_id, dx_code, finalized_at DESC);

CREATE TABLE treatment_outcomes (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id   uuid NOT NULL REFERENCES consultations(id),
  facility_id       uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  outcome           text NOT NULL CHECK (outcome IN ('RESOLVED','IMPROVED','UNCHANGED','WORSENED')),
  adverse_effects   text,
  notes             text,
  recorded_by       uuid NOT NULL REFERENCES users(id),
  recorded_at       timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_outcomes_consultation ON treatment_outcomes(consultation_id, recorded_at DESC);
CREATE TRIGGER trg_outcomes_immutable BEFORE UPDATE OR DELETE ON treatment_outcomes
  FOR EACH ROW EXECUTE FUNCTION cf_audit_immutable();

-- ---------------------------------------------------------------------------
-- ClinicFlow network: de-identified, coded case records from participating clinics.
-- NOT facility-scoped (it is the shared pool) and therefore readable only through
-- the aggregation service, which enforces minimum sample sizes. Contains:
--  * no patient id, name, number, date of birth or free text
--  * keyed HMAC pseudonyms for clinic and doctor (not reversible without the server key)
--  * month-level period only
-- ---------------------------------------------------------------------------
CREATE TABLE network_case_records (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  source_key       text NOT NULL UNIQUE,   -- HMAC(case id) — lets outcomes update the record
  facility_key     text NOT NULL,
  doctor_key       text NOT NULL,
  period_month     date NOT NULL CHECK (extract(day FROM period_month) = 1),
  specialty        text NOT NULL,
  dx_code          text NOT NULL,
  severity         text,
  age_band         text NOT NULL,
  sex              text,
  pregnancy_status text,
  duration_band    text,
  generic_codes    text[] NOT NULL DEFAULT '{}',
  regimen          jsonb NOT NULL DEFAULT '[]',  -- [{"generic":"...","route":"...","frequency":"...","durationDays":n}]
  outcome          text,
  withdrawn_at     timestamptz,
  created_at       timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_network_dx ON network_case_records(dx_code, severity, age_band) WHERE withdrawn_at IS NULL;

-- ---------------------------------------------------------------------------
-- Facility isolation for the new patient-level tables
-- ---------------------------------------------------------------------------
DO $$
DECLARE
  t text;
BEGIN
  FOREACH t IN ARRAY ARRAY['derm_assessments','clinical_photos','cds_analysis_runs','suggestion_snapshots',
                           'suggestion_decisions','safety_overrides','prescribing_cases','treatment_outcomes'] LOOP
    EXECUTE format('CREATE INDEX %I ON %I(facility_id)', 'ix_' || t || '_facility', t);
    EXECUTE format('ALTER TABLE %I ENABLE ROW LEVEL SECURITY', t);
    EXECUTE format('ALTER TABLE %I FORCE ROW LEVEL SECURITY', t);
    EXECUTE format('CREATE POLICY facility_isolation ON %I USING (cf_rls_bypass() OR facility_id = cf_current_facility()) '
                   'WITH CHECK (cf_rls_bypass() OR facility_id = cf_current_facility())', t);
  END LOOP;
END $$;

DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'clinicflow_app') THEN
    EXECUTE 'GRANT SELECT, INSERT, UPDATE ON derm_assessments, clinical_photos, medication_interactions, cds_conditions,
             cds_condition_features, cds_condition_content, cds_red_flags, cds_analysis_runs, suggestion_snapshots,
             suggestion_decisions, safety_overrides, prescribing_cases, network_case_records TO clinicflow_app';
    EXECUTE 'GRANT SELECT, INSERT ON treatment_outcomes TO clinicflow_app';
    EXECUTE 'GRANT DELETE ON suggestion_decisions TO clinicflow_app';
  END IF;
END $$;
