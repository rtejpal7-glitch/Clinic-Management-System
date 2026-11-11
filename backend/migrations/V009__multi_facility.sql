-- ClinicFlow V009: multiple clinics (facilities) in one installation, isolated by
-- PostgreSQL row-level security (RLS).
--
-- Every clinical, financial and ABDM table gets facility_id. The API sets the
-- caller's facility per database connection (set_config('app.facility_id', ...)),
-- and RLS policies make rows of other facilities invisible and unwritable — even
-- if application code forgets a WHERE clause. Background jobs that legitimately
-- span facilities set app.rls_bypass = 'on'.
--
-- Deployment-level settings (timezone, ID prefixes, idle timeout, duplicate rules)
-- stay in clinic_settings; clinic identity (name, address, registrations) moves to
-- facilities.

CREATE TABLE facilities (
  id                      uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code                    text NOT NULL UNIQUE CHECK (code ~ '^[A-Z0-9]{2,12}$'),
  name                    text NOT NULL CHECK (length(btrim(name)) >= 2),
  specialty_code          text NOT NULL DEFAULT 'DERMATOLOGY' CHECK (specialty_code IN ('DERMATOLOGY','GENERAL')),
  address                 text,
  city                    text,
  state                   text,
  pincode                 text CHECK (pincode IS NULL OR pincode ~ '^[1-9][0-9]{5}$'),
  phone                   text,
  email                   text,
  registration_number     text,
  gstin                   text,
  hfr_id                  text,
  -- ClinicFlow network (cross-clinic de-identified prescribing analytics).
  -- Off by default; switching on requires a recorded data-sharing agreement.
  network_sharing_enabled boolean NOT NULL DEFAULT false,
  network_agreement_ref   text,
  network_enabled_at      timestamptz,
  network_enabled_by      uuid,
  is_active               boolean NOT NULL DEFAULT true,
  created_at              timestamptz NOT NULL DEFAULT now(),
  updated_at              timestamptz NOT NULL DEFAULT now(),
  CHECK (NOT network_sharing_enabled OR network_agreement_ref IS NOT NULL)
);
CREATE TRIGGER trg_facilities_touch BEFORE UPDATE ON facilities FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

-- The existing clinic becomes facility #1 (deterministic id, used by docs/tests).
INSERT INTO facilities (id, code, name, address, city, state, pincode, phone, email, registration_number, gstin, hfr_id)
SELECT '00000000-0000-4000-8000-000000000001', 'MAIN', clinic_name, address, city, state, pincode, phone, email,
       registration_number, gstin, hfr_id
FROM clinic_settings WHERE id = 1;

CREATE OR REPLACE FUNCTION cf_current_facility() RETURNS uuid LANGUAGE sql STABLE AS $$
  SELECT nullif(current_setting('app.facility_id', true), '')::uuid
$$;
CREATE OR REPLACE FUNCTION cf_rls_bypass() RETURNS boolean LANGUAGE sql STABLE AS $$
  SELECT coalesce(current_setting('app.rls_bypass', true), '') = 'on'
$$;

-- Users belong to one facility (login is global; data access is per facility).
ALTER TABLE users ADD COLUMN facility_id uuid REFERENCES facilities(id);
UPDATE users SET facility_id = '00000000-0000-4000-8000-000000000001';
ALTER TABLE users ALTER COLUMN facility_id SET NOT NULL;
CREATE INDEX ix_users_facility ON users(facility_id);

-- Roles: NULL facility = system role shared by all clinics (only platform admins may
-- change its permissions); custom roles belong to one facility.
ALTER TABLE roles ADD COLUMN facility_id uuid REFERENCES facilities(id);
ALTER TABLE roles DROP CONSTRAINT roles_code_key;
CREATE UNIQUE INDEX ux_roles_code ON roles (coalesce(facility_id, '00000000-0000-0000-0000-000000000000'::uuid), code);

-- Audit entries record the facility context they were written in (not part of the hash).
ALTER TABLE audit_logs ADD COLUMN facility_id uuid;
ALTER TABLE audit_logs ALTER COLUMN facility_id SET DEFAULT cf_current_facility();
CREATE INDEX ix_audit_facility ON audit_logs(facility_id, occurred_at DESC);

-- Allow the backfill below to touch audit-protected tables (payments, addenda) once.
ALTER TABLE payments DISABLE TRIGGER trg_payments_immutable;
ALTER TABLE consultation_addenda DISABLE TRIGGER trg_addenda_immutable;
ALTER TABLE consultations DISABLE TRIGGER trg_consultations_lock;
ALTER TABLE prescriptions DISABLE TRIGGER trg_prescriptions_lock;
ALTER TABLE vital_signs DISABLE TRIGGER trg_vitals_lock;
ALTER TABLE consultation_diagnoses DISABLE TRIGGER trg_diagnoses_lock;
ALTER TABLE prescription_items DISABLE TRIGGER trg_rx_items_lock;

DO $$
DECLARE
  t text;
  tables text[] := ARRAY[
    'patients','patient_change_history','patient_identifiers','patient_allergies',
    'doctors','doctor_schedules','appointments','appointment_status_history','queue_tokens','notifications',
    'consultations','consultation_addenda','vital_signs','consultation_diagnoses',
    'prescriptions','prescription_items','investigation_orders',
    'service_catalogue','bills','bill_items','payments',
    'patient_consent_records','abdm_transactions','abdm_care_contexts'];
BEGIN
  FOREACH t IN ARRAY tables LOOP
    EXECUTE format('ALTER TABLE %I ADD COLUMN facility_id uuid REFERENCES facilities(id)', t);
    EXECUTE format('UPDATE %I SET facility_id = %L', t, '00000000-0000-4000-8000-000000000001');
    EXECUTE format('ALTER TABLE %I ALTER COLUMN facility_id SET NOT NULL', t);
    EXECUTE format('ALTER TABLE %I ALTER COLUMN facility_id SET DEFAULT cf_current_facility()', t);
    EXECUTE format('CREATE INDEX %I ON %I(facility_id)', 'ix_' || t || '_facility', t);
    EXECUTE format('ALTER TABLE %I ENABLE ROW LEVEL SECURITY', t);
    EXECUTE format('ALTER TABLE %I FORCE ROW LEVEL SECURITY', t);
    EXECUTE format('CREATE POLICY facility_isolation ON %I USING (cf_rls_bypass() OR facility_id = cf_current_facility()) '
                   'WITH CHECK (cf_rls_bypass() OR facility_id = cf_current_facility())', t);
  END LOOP;
END $$;

ALTER TABLE payments ENABLE TRIGGER trg_payments_immutable;
ALTER TABLE consultation_addenda ENABLE TRIGGER trg_addenda_immutable;
ALTER TABLE consultations ENABLE TRIGGER trg_consultations_lock;
ALTER TABLE prescriptions ENABLE TRIGGER trg_prescriptions_lock;
ALTER TABLE vital_signs ENABLE TRIGGER trg_vitals_lock;
ALTER TABLE consultation_diagnoses ENABLE TRIGGER trg_diagnoses_lock;
ALTER TABLE prescription_items ENABLE TRIGGER trg_rx_items_lock;

-- Uniqueness that must now be per clinic
DROP INDEX ux_identifier_active;
CREATE UNIQUE INDEX ux_identifier_active ON patient_identifiers(facility_id, id_type, value_hmac)
  WHERE verification_status IN ('PENDING','VERIFIED');
ALTER TABLE service_catalogue DROP CONSTRAINT service_catalogue_code_key;
CREATE UNIQUE INDEX ux_services_code ON service_catalogue(facility_id, code);

-- Platform administration (creating clinics, shared system roles)
INSERT INTO permissions (code, module, description) VALUES
  ('platform.manage', 'platform', 'Create clinics and manage shared system roles across clinics');
INSERT INTO roles (code, name, description, is_system) VALUES
  ('PLATFORM_ADMIN', 'Platform administrator', 'Manages clinics on this ClinicFlow installation', true);
INSERT INTO role_permissions (role_id, permission_id)
SELECT r.id, p.id FROM roles r, permissions p WHERE r.code = 'PLATFORM_ADMIN' AND p.code = 'platform.manage';
-- The original bootstrap administrator (if any) becomes platform administrator.
INSERT INTO user_roles (user_id, role_id)
SELECT u.id, r.id FROM users u CROSS JOIN roles r
WHERE r.code = 'PLATFORM_ADMIN'
  AND u.id = (SELECT ur.user_id FROM user_roles ur JOIN roles x ON x.id = ur.role_id WHERE x.code = 'ADMIN'
              ORDER BY (SELECT created_at FROM users WHERE id = ur.user_id) LIMIT 1);

-- Grants for objects created in this migration
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'clinicflow_app') THEN
    EXECUTE 'GRANT SELECT, INSERT, UPDATE ON facilities TO clinicflow_app';
    EXECUTE 'GRANT DELETE ON role_permissions, user_roles TO clinicflow_app';
  END IF;
END $$;
