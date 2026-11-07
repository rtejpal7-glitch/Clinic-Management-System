-- ClinicFlow V001: core security, configuration and audit tables
-- Requires PostgreSQL 13+ (gen_random_uuid in core). Developed against PostgreSQL 16.

CREATE EXTENSION IF NOT EXISTS pg_trgm;     -- fuzzy name matching (duplicate detection, search)
CREATE EXTENSION IF NOT EXISTS btree_gist;  -- exclusion constraint for double-booking prevention

-- Shared trigger: maintain updated_at
CREATE OR REPLACE FUNCTION cf_touch_updated_at() RETURNS trigger AS $$
BEGIN
  NEW.updated_at := now();
  RETURN NEW;
END $$ LANGUAGE plpgsql;

-- ---------------------------------------------------------------------------
-- Roles and permissions (RBAC). Deny by default: a user can do only what a
-- permission explicitly granted to one of their roles allows.
-- ---------------------------------------------------------------------------
CREATE TABLE roles (
  id           uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code         text NOT NULL UNIQUE CHECK (code ~ '^[A-Z][A-Z0-9_]{1,39}$'),
  name         text NOT NULL,
  description  text,
  is_system    boolean NOT NULL DEFAULT false,
  created_at   timestamptz NOT NULL DEFAULT now(),
  updated_at   timestamptz NOT NULL DEFAULT now()
);
CREATE TRIGGER trg_roles_touch BEFORE UPDATE ON roles FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE permissions (
  id           uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code         text NOT NULL UNIQUE CHECK (code ~ '^[a-z][a-z_.]{2,63}$'),
  module       text NOT NULL,
  description  text NOT NULL
);

CREATE TABLE role_permissions (
  role_id       uuid NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
  permission_id uuid NOT NULL REFERENCES permissions(id) ON DELETE CASCADE,
  granted_at    timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (role_id, permission_id)
);

CREATE TABLE users (
  id                   uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  username             text NOT NULL CHECK (username ~ '^[a-zA-Z0-9._-]{3,40}$'),
  full_name            text NOT NULL CHECK (length(full_name) BETWEEN 2 AND 120),
  email                text,
  phone                text,
  password_hash        text NOT NULL,              -- Argon2id encoded hash, never plaintext
  is_active            boolean NOT NULL DEFAULT true,
  failed_login_count   integer NOT NULL DEFAULT 0,
  locked_until         timestamptz,
  must_change_password boolean NOT NULL DEFAULT false,
  last_login_at        timestamptz,
  password_changed_at  timestamptz NOT NULL DEFAULT now(),
  created_at           timestamptz NOT NULL DEFAULT now(),
  updated_at           timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX ux_users_username_ci ON users (lower(username));
CREATE TRIGGER trg_users_touch BEFORE UPDATE ON users FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE user_roles (
  user_id    uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  role_id    uuid NOT NULL REFERENCES roles(id) ON DELETE RESTRICT,
  assigned_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (user_id, role_id)
);
CREATE INDEX ix_user_roles_role ON user_roles(role_id);

-- Server-side sessions: only a SHA-256 hash of the bearer token is stored,
-- so a database leak does not expose usable tokens. Supports logout/revocation.
CREATE TABLE user_sessions (
  id            uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id       uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  token_hash    text NOT NULL UNIQUE,
  created_at    timestamptz NOT NULL DEFAULT now(),
  last_seen_at  timestamptz NOT NULL DEFAULT now(),
  expires_at    timestamptz NOT NULL,
  revoked_at    timestamptz,
  revoke_reason text,
  ip_address    text,
  user_agent    text
);
CREATE INDEX ix_sessions_user ON user_sessions(user_id) WHERE revoked_at IS NULL;

-- ---------------------------------------------------------------------------
-- Clinic configuration (single clinic deployment => singleton row)
-- ---------------------------------------------------------------------------
CREATE TABLE clinic_settings (
  id                         smallint PRIMARY KEY DEFAULT 1 CHECK (id = 1),
  clinic_name                text NOT NULL DEFAULT 'ClinicFlow Demo Clinic',
  address                    text,
  city                       text,
  state                      text,
  pincode                    text CHECK (pincode IS NULL OR pincode ~ '^[1-9][0-9]{5}$'),
  phone                      text,
  email                      text,
  registration_number        text,      -- clinical establishment registration
  gstin                      text,
  hfr_id                     text,      -- ABDM Health Facility Registry ID, once onboarded
  timezone                   text NOT NULL DEFAULT 'Asia/Kolkata',
  currency                   text NOT NULL DEFAULT 'INR',
  patient_id_prefix          text NOT NULL DEFAULT 'CF' CHECK (patient_id_prefix ~ '^[A-Z]{2,6}$'),
  invoice_prefix             text NOT NULL DEFAULT 'INV' CHECK (invoice_prefix ~ '^[A-Z]{2,6}$'),
  default_slot_minutes       integer NOT NULL DEFAULT 15 CHECK (default_slot_minutes BETWEEN 5 AND 120),
  session_idle_minutes       integer NOT NULL DEFAULT 30 CHECK (session_idle_minutes BETWEEN 5 AND 480),
  -- configurable duplicate-patient matching rules (weights and threshold)
  duplicate_rules            jsonb NOT NULL DEFAULT '{
      "threshold": 60,
      "nameSimilarityMin": 0.45,
      "weights": {"mobile": 40, "dateOfBirth": 30, "name": 30, "gender": 0}
    }'::jsonb,
  updated_at                 timestamptz NOT NULL DEFAULT now(),
  updated_by                 uuid REFERENCES users(id)
);
INSERT INTO clinic_settings (id) VALUES (1);

-- Gap-free, concurrency-safe counters (patient numbers, invoice numbers, queue tokens)
CREATE TABLE id_counters (
  counter_name text NOT NULL,
  period       text NOT NULL,
  last_value   bigint NOT NULL DEFAULT 0,
  PRIMARY KEY (counter_name, period)
);

-- ---------------------------------------------------------------------------
-- Audit log: append-only, hash-chained for tamper evidence.
-- ---------------------------------------------------------------------------
CREATE TABLE audit_logs (
  id           bigserial PRIMARY KEY,
  occurred_at  timestamptz NOT NULL,
  user_id      uuid,                 -- no FK: audit must survive user changes
  username     text,
  roles        text,
  action       text NOT NULL,
  entity_type  text,
  entity_id    text,
  patient_id   uuid,
  outcome      text NOT NULL CHECK (outcome IN ('SUCCESS','FAILURE','DENIED')),
  ip_address   text,
  user_agent   text,
  details      jsonb,
  prev_hash    text NOT NULL,
  hash         text NOT NULL
);
CREATE INDEX ix_audit_occurred ON audit_logs(occurred_at DESC);
CREATE INDEX ix_audit_entity ON audit_logs(entity_type, entity_id);
CREATE INDEX ix_audit_user ON audit_logs(user_id, occurred_at DESC);
CREATE INDEX ix_audit_patient ON audit_logs(patient_id, occurred_at DESC) WHERE patient_id IS NOT NULL;
CREATE INDEX ix_audit_action ON audit_logs(action);

CREATE OR REPLACE FUNCTION cf_audit_immutable() RETURNS trigger AS $$
BEGIN
  RAISE EXCEPTION 'audit_logs is append-only' USING ERRCODE = 'P0001';
END $$ LANGUAGE plpgsql;
CREATE TRIGGER trg_audit_no_update BEFORE UPDATE OR DELETE ON audit_logs
  FOR EACH ROW EXECUTE FUNCTION cf_audit_immutable();
CREATE TRIGGER trg_audit_no_truncate BEFORE TRUNCATE ON audit_logs
  FOR EACH STATEMENT EXECUTE FUNCTION cf_audit_immutable();
