-- ClinicFlow V004: catalogues, consultations (EMR), prescriptions, investigation orders

CREATE TABLE medication_catalogue (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  generic_name    text NOT NULL,
  brand_name      text,
  strength        text NOT NULL,
  dosage_form     text NOT NULL,     -- TABLET, CAPSULE, SYRUP, INJECTION, ...
  default_route   text NOT NULL DEFAULT 'ORAL',
  drug_schedule   text,              -- Drugs & Cosmetics Rules schedule (H, H1, X, G, OTC)
  is_active       boolean NOT NULL DEFAULT true,
  created_at      timestamptz NOT NULL DEFAULT now(),
  updated_at      timestamptz NOT NULL DEFAULT now(),
  UNIQUE NULLS NOT DISTINCT (generic_name, brand_name, strength, dosage_form)
);
CREATE INDEX ix_medcat_search ON medication_catalogue
  USING gin ((lower(generic_name || ' ' || coalesce(brand_name, ''))) gin_trgm_ops);
CREATE TRIGGER trg_medcat_touch BEFORE UPDATE ON medication_catalogue FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE investigation_catalogue (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code        text NOT NULL UNIQUE,
  name        text NOT NULL,
  category    text NOT NULL CHECK (category IN ('LAB','RADIOLOGY','OTHER')),
  loinc_code  text,
  specimen    text,
  is_active   boolean NOT NULL DEFAULT true
);
CREATE INDEX ix_invcat_search ON investigation_catalogue USING gin (lower(name) gin_trgm_ops);

CREATE TABLE diagnosis_codes (
  code_system text NOT NULL CHECK (code_system IN ('ICD-10','SNOMED-CT')),
  code        text NOT NULL,
  display     text NOT NULL,
  PRIMARY KEY (code_system, code)
);
CREATE INDEX ix_dxcodes_search ON diagnosis_codes USING gin (lower(display) gin_trgm_ops);

CREATE TABLE consultations (
  id                        uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  appointment_id            uuid UNIQUE REFERENCES appointments(id) ON DELETE RESTRICT,
  patient_id                uuid NOT NULL REFERENCES patients(id) ON DELETE RESTRICT,
  doctor_id                 uuid NOT NULL REFERENCES doctors(id) ON DELETE RESTRICT,
  status                    text NOT NULL DEFAULT 'DRAFT' CHECK (status IN ('DRAFT','FINALIZED')),
  chief_complaint           text,
  history_present_illness   text,
  past_medical_history      text,
  current_medications       text,
  examination_notes         text,
  treatment_plan            text,
  follow_up_instructions    text,
  follow_up_date            date,
  started_at                timestamptz NOT NULL DEFAULT now(),
  finalized_at              timestamptz,
  finalized_by              uuid REFERENCES users(id),
  version                   integer NOT NULL DEFAULT 1,
  created_at                timestamptz NOT NULL DEFAULT now(),
  updated_at                timestamptz NOT NULL DEFAULT now(),
  CHECK (status = 'DRAFT' OR finalized_at IS NOT NULL)
);
CREATE INDEX ix_consultations_patient ON consultations(patient_id, started_at DESC);
CREATE INDEX ix_consultations_doctor ON consultations(doctor_id, started_at DESC);
CREATE TRIGGER trg_consultations_touch BEFORE UPDATE ON consultations FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

-- Finalized clinical records are locked at the database level. Corrections are
-- recorded as addenda, never by editing the original.
CREATE OR REPLACE FUNCTION cf_consultation_lock() RETURNS trigger AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    RAISE EXCEPTION 'clinical records cannot be deleted' USING ERRCODE = 'P0001';
  END IF;
  IF OLD.status = 'FINALIZED' THEN
    RAISE EXCEPTION 'finalized consultation is locked; add an addendum instead' USING ERRCODE = 'P0001';
  END IF;
  RETURN NEW;
END $$ LANGUAGE plpgsql;
CREATE TRIGGER trg_consultations_lock BEFORE UPDATE OR DELETE ON consultations
  FOR EACH ROW EXECUTE FUNCTION cf_consultation_lock();

-- Child rows of a consultation can only change while it is a draft
CREATE OR REPLACE FUNCTION cf_consultation_child_lock() RETURNS trigger AS $$
DECLARE
  cid uuid;
  st  text;
BEGIN
  cid := CASE WHEN TG_OP = 'DELETE' THEN OLD.consultation_id ELSE NEW.consultation_id END;
  SELECT status INTO st FROM consultations WHERE id = cid;
  IF st = 'FINALIZED' THEN
    RAISE EXCEPTION 'finalized consultation is locked' USING ERRCODE = 'P0001';
  END IF;
  IF TG_OP = 'DELETE' THEN RETURN OLD; END IF;
  RETURN NEW;
END $$ LANGUAGE plpgsql;

CREATE TABLE consultation_addenda (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id) ON DELETE RESTRICT,
  author_id        uuid NOT NULL REFERENCES users(id),
  reason           text NOT NULL CHECK (length(btrim(reason)) >= 3),
  content          text NOT NULL CHECK (length(btrim(content)) >= 3),
  created_at       timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_addenda_consultation ON consultation_addenda(consultation_id, created_at);
CREATE TRIGGER trg_addenda_immutable BEFORE UPDATE OR DELETE ON consultation_addenda
  FOR EACH ROW EXECUTE FUNCTION cf_audit_immutable();

CREATE TABLE vital_signs (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id   uuid NOT NULL UNIQUE REFERENCES consultations(id) ON DELETE RESTRICT,
  patient_id        uuid NOT NULL REFERENCES patients(id),
  recorded_by       uuid REFERENCES users(id),
  recorded_at       timestamptz NOT NULL DEFAULT now(),
  bp_systolic       smallint CHECK (bp_systolic BETWEEN 40 AND 300),
  bp_diastolic      smallint CHECK (bp_diastolic BETWEEN 20 AND 200),
  pulse             smallint CHECK (pulse BETWEEN 20 AND 250),
  temperature_c     numeric(4,1) CHECK (temperature_c BETWEEN 30 AND 45),
  respiratory_rate  smallint CHECK (respiratory_rate BETWEEN 4 AND 80),
  spo2              smallint CHECK (spo2 BETWEEN 50 AND 100),
  height_cm         numeric(5,1) CHECK (height_cm BETWEEN 30 AND 250),
  weight_kg         numeric(5,1) CHECK (weight_kg BETWEEN 1 AND 400),
  CHECK (bp_systolic IS NULL OR bp_diastolic IS NULL OR bp_systolic > bp_diastolic)
);
CREATE INDEX ix_vitals_patient ON vital_signs(patient_id, recorded_at DESC);
CREATE TRIGGER trg_vitals_lock BEFORE INSERT OR UPDATE OR DELETE ON vital_signs
  FOR EACH ROW EXECUTE FUNCTION cf_consultation_child_lock();

CREATE TABLE consultation_diagnoses (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id) ON DELETE RESTRICT,
  code_system      text NOT NULL CHECK (code_system IN ('ICD-10','SNOMED-CT','FREE_TEXT')),
  code             text,
  display          text NOT NULL CHECK (length(btrim(display)) >= 2),
  diagnosis_type   text NOT NULL DEFAULT 'PRIMARY' CHECK (diagnosis_type IN ('PRIMARY','SECONDARY','PROVISIONAL')),
  notes            text,
  sort_order       integer NOT NULL DEFAULT 0,
  created_at       timestamptz NOT NULL DEFAULT now(),
  CHECK (code_system = 'FREE_TEXT' OR code IS NOT NULL)
);
CREATE INDEX ix_diagnoses_consultation ON consultation_diagnoses(consultation_id);
CREATE TRIGGER trg_diagnoses_lock BEFORE INSERT OR UPDATE OR DELETE ON consultation_diagnoses
  FOR EACH ROW EXECUTE FUNCTION cf_consultation_child_lock();

CREATE TABLE prescriptions (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL UNIQUE REFERENCES consultations(id) ON DELETE RESTRICT,
  patient_id       uuid NOT NULL REFERENCES patients(id),
  doctor_id        uuid NOT NULL REFERENCES doctors(id),
  status           text NOT NULL DEFAULT 'DRAFT' CHECK (status IN ('DRAFT','FINALIZED')),
  advice           text,              -- non-medication instructions (diet, rest, lifestyle)
  finalized_at     timestamptz,
  created_at       timestamptz NOT NULL DEFAULT now(),
  updated_at       timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_prescriptions_patient ON prescriptions(patient_id, created_at DESC);
CREATE TRIGGER trg_prescriptions_touch BEFORE UPDATE ON prescriptions FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE OR REPLACE FUNCTION cf_prescription_lock() RETURNS trigger AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    RAISE EXCEPTION 'prescriptions cannot be deleted' USING ERRCODE = 'P0001';
  END IF;
  IF OLD.status = 'FINALIZED' THEN
    RAISE EXCEPTION 'finalized prescription is locked' USING ERRCODE = 'P0001';
  END IF;
  RETURN NEW;
END $$ LANGUAGE plpgsql;
CREATE TRIGGER trg_prescriptions_lock BEFORE UPDATE OR DELETE ON prescriptions
  FOR EACH ROW EXECUTE FUNCTION cf_prescription_lock();

CREATE TABLE prescription_items (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  prescription_id  uuid NOT NULL REFERENCES prescriptions(id) ON DELETE RESTRICT,
  medication_id    uuid REFERENCES medication_catalogue(id),
  medicine_name    text NOT NULL,     -- snapshot so later catalogue edits don't alter history
  strength         text,
  dosage_form      text,
  dose             text NOT NULL,     -- e.g. "1 tablet", "5 ml"
  route            text NOT NULL,
  frequency        text NOT NULL,     -- e.g. "1-0-1", "TDS", "SOS"
  duration_days    integer CHECK (duration_days IS NULL OR duration_days BETWEEN 1 AND 365),
  quantity         numeric(8,2) CHECK (quantity IS NULL OR quantity > 0),
  instructions     text,
  sort_order       integer NOT NULL DEFAULT 0
);
CREATE INDEX ix_rx_items ON prescription_items(prescription_id);

CREATE OR REPLACE FUNCTION cf_prescription_child_lock() RETURNS trigger AS $$
DECLARE
  st text;
BEGIN
  SELECT status INTO st FROM prescriptions
   WHERE id = CASE WHEN TG_OP = 'DELETE' THEN OLD.prescription_id ELSE NEW.prescription_id END;
  IF st = 'FINALIZED' THEN
    RAISE EXCEPTION 'finalized prescription is locked' USING ERRCODE = 'P0001';
  END IF;
  IF TG_OP = 'DELETE' THEN RETURN OLD; END IF;
  RETURN NEW;
END $$ LANGUAGE plpgsql;
CREATE TRIGGER trg_rx_items_lock BEFORE INSERT OR UPDATE OR DELETE ON prescription_items
  FOR EACH ROW EXECUTE FUNCTION cf_prescription_child_lock();

-- CPOE investigation orders (lab / radiology)
CREATE TABLE investigation_orders (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id   uuid NOT NULL REFERENCES consultations(id) ON DELETE RESTRICT,
  patient_id        uuid NOT NULL REFERENCES patients(id),
  doctor_id         uuid NOT NULL REFERENCES doctors(id),
  investigation_id  uuid REFERENCES investigation_catalogue(id),
  name              text NOT NULL,
  category          text NOT NULL CHECK (category IN ('LAB','RADIOLOGY','OTHER')),
  loinc_code        text,
  priority          text NOT NULL DEFAULT 'ROUTINE' CHECK (priority IN ('ROUTINE','URGENT')),
  clinical_notes    text,
  status            text NOT NULL DEFAULT 'ORDERED' CHECK (status IN ('ORDERED','CANCELLED','COMPLETED')),
  ordered_at        timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_orders_patient ON investigation_orders(patient_id, ordered_at DESC);
CREATE INDEX ix_orders_consultation ON investigation_orders(consultation_id);
CREATE TRIGGER trg_orders_lock BEFORE INSERT OR DELETE ON investigation_orders
  FOR EACH ROW EXECUTE FUNCTION cf_consultation_child_lock();
