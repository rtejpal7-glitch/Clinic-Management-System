-- ClinicFlow V002: patient master, identifiers, allergies, change history

CREATE TABLE patients (
  id                          uuid PRIMARY KEY DEFAULT gen_random_uuid(),  -- immutable internal identifier
  patient_number              text NOT NULL UNIQUE,                         -- human-readable, immutable (e.g. CF-2026-000123)
  full_name                   text NOT NULL CHECK (length(btrim(full_name)) BETWEEN 2 AND 120),
  date_of_birth               date NOT NULL CHECK (date_of_birth > DATE '1900-01-01'),
  dob_is_estimated            boolean NOT NULL DEFAULT false,
  gender                      text NOT NULL CHECK (gender IN ('MALE','FEMALE','OTHER','UNDISCLOSED')),
  mobile                      text NOT NULL CHECK (mobile ~ '^[6-9][0-9]{9}$'),
  email                       text CHECK (email IS NULL OR email ~* '^[^@\s]+@[^@\s]+\.[^@\s]+$'),
  address_line                text,
  city                        text,
  district                    text,
  state                       text,
  pincode                     text CHECK (pincode IS NULL OR pincode ~ '^[1-9][0-9]{5}$'),
  emergency_contact_name      text,
  emergency_contact_relation  text,
  emergency_contact_phone     text CHECK (emergency_contact_phone IS NULL OR emergency_contact_phone ~ '^[6-9][0-9]{9}$'),
  blood_group                 text CHECK (blood_group IS NULL OR blood_group IN ('A+','A-','B+','B-','AB+','AB-','O+','O-')),
  marital_status              text CHECK (marital_status IS NULL OR marital_status IN ('SINGLE','MARRIED','DIVORCED','WIDOWED','OTHER')),
  occupation                  text,
  preferred_language          text,
  is_active                   boolean NOT NULL DEFAULT true,
  version                     integer NOT NULL DEFAULT 1,
  created_at                  timestamptz NOT NULL DEFAULT now(),
  created_by                  uuid REFERENCES users(id),
  updated_at                  timestamptz NOT NULL DEFAULT now(),
  updated_by                  uuid REFERENCES users(id)
);
CREATE INDEX ix_patients_mobile ON patients(mobile);
CREATE INDEX ix_patients_dob ON patients(date_of_birth);
CREATE INDEX ix_patients_name_trgm ON patients USING gin (lower(full_name) gin_trgm_ops);
CREATE INDEX ix_patients_created ON patients(created_at DESC);
CREATE TRIGGER trg_patients_touch BEFORE UPDATE ON patients FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE OR REPLACE FUNCTION cf_patient_ids_immutable() RETURNS trigger AS $$
BEGIN
  IF NEW.id <> OLD.id OR NEW.patient_number <> OLD.patient_number THEN
    RAISE EXCEPTION 'patient identifiers are immutable' USING ERRCODE = 'P0001';
  END IF;
  RETURN NEW;
END $$ LANGUAGE plpgsql;
CREATE TRIGGER trg_patients_ids_immutable BEFORE UPDATE ON patients
  FOR EACH ROW EXECUTE FUNCTION cf_patient_ids_immutable();

-- Field-level change history for demographic updates
CREATE TABLE patient_change_history (
  id          bigserial PRIMARY KEY,
  patient_id  uuid NOT NULL REFERENCES patients(id) ON DELETE RESTRICT,
  changed_at  timestamptz NOT NULL DEFAULT now(),
  changed_by  uuid REFERENCES users(id),
  field_name  text NOT NULL,
  old_value   text,
  new_value   text,
  reason      text
);
CREATE INDEX ix_patient_history ON patient_change_history(patient_id, changed_at DESC);

-- External identifiers (ABHA number / ABHA address). Values are encrypted at
-- rest (AES-256-GCM, application-level); lookups use an HMAC blind index.
-- The internal patient id is always the primary key; these are never used as keys.
CREATE TABLE patient_identifiers (
  id                   uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  patient_id           uuid NOT NULL REFERENCES patients(id) ON DELETE RESTRICT,
  id_type              text NOT NULL CHECK (id_type IN ('ABHA_NUMBER','ABHA_ADDRESS')),
  value_ciphertext     text NOT NULL,
  value_hmac           text NOT NULL,
  value_masked         text NOT NULL,
  verification_status  text NOT NULL DEFAULT 'PENDING'
                       CHECK (verification_status IN ('PENDING','VERIFIED','FAILED','UNLINKED')),
  verification_method  text,
  verified_at          timestamptz,
  is_mock              boolean NOT NULL,     -- true => simulated by mock adapter, NOT a real ABDM verification
  linked_by            uuid REFERENCES users(id),
  unlinked_at          timestamptz,
  unlinked_by          uuid REFERENCES users(id),
  unlink_reason        text,
  created_at           timestamptz NOT NULL DEFAULT now(),
  updated_at           timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX ux_identifier_active ON patient_identifiers(id_type, value_hmac)
  WHERE verification_status IN ('PENDING','VERIFIED');
CREATE INDEX ix_identifiers_patient ON patient_identifiers(patient_id);
CREATE TRIGGER trg_identifiers_touch BEFORE UPDATE ON patient_identifiers FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE patient_allergies (
  id           uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  patient_id   uuid NOT NULL REFERENCES patients(id) ON DELETE RESTRICT,
  substance    text NOT NULL CHECK (length(btrim(substance)) BETWEEN 2 AND 120),
  category     text NOT NULL CHECK (category IN ('MEDICATION','FOOD','ENVIRONMENT','OTHER')),
  reaction     text,
  severity     text NOT NULL CHECK (severity IN ('MILD','MODERATE','SEVERE','UNKNOWN')),
  status       text NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE','INACTIVE','ENTERED_IN_ERROR')),
  recorded_by  uuid REFERENCES users(id),
  created_at   timestamptz NOT NULL DEFAULT now(),
  updated_at   timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_allergies_patient ON patient_allergies(patient_id);
CREATE TRIGGER trg_allergies_touch BEFORE UPDATE ON patient_allergies FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();
