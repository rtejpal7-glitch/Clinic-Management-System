-- ClinicFlow V006: ABDM interoperability tracking (consent and gateway transactions).
-- Every row carries is_mock so simulated activity can never be mistaken for a
-- real ABDM transaction.

CREATE TABLE patient_consent_records (
  id                     uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  patient_id             uuid NOT NULL REFERENCES patients(id) ON DELETE RESTRICT,
  direction              text NOT NULL CHECK (direction IN ('HIU_REQUEST','HIP_SHARE')),
  purpose_code           text NOT NULL CHECK (purpose_code IN ('CAREMGT','BTG','PUBHLTH','HPAYMT','DSRCH','PATRQT')),
  hi_types               text[] NOT NULL CHECK (cardinality(hi_types) > 0),
  date_from              date NOT NULL,
  date_to                date NOT NULL,
  expires_at             timestamptz NOT NULL,
  status                 text NOT NULL DEFAULT 'REQUESTED'
                         CHECK (status IN ('REQUESTED','GRANTED','DENIED','EXPIRED','REVOKED','FAILED')),
  status_reason          text,
  consent_request_ref    text,       -- gateway consent-request id
  consent_artefact_ref   text,       -- consent artefact id once granted
  requested_by           uuid REFERENCES users(id),
  requested_at           timestamptz NOT NULL DEFAULT now(),
  responded_at           timestamptz,
  revoked_at             timestamptz,
  is_mock                boolean NOT NULL,
  created_at             timestamptz NOT NULL DEFAULT now(),
  updated_at             timestamptz NOT NULL DEFAULT now(),
  CHECK (date_to >= date_from)
);
CREATE INDEX ix_consents_patient ON patient_consent_records(patient_id, requested_at DESC);
CREATE INDEX ix_consents_expiry ON patient_consent_records(expires_at) WHERE status IN ('REQUESTED','GRANTED');
CREATE TRIGGER trg_consents_touch BEFORE UPDATE ON patient_consent_records FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE abdm_transactions (
  id                 uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  request_id         uuid NOT NULL UNIQUE DEFAULT gen_random_uuid(),   -- idempotency key sent to the gateway
  txn_type           text NOT NULL CHECK (txn_type IN
                       ('ABHA_VERIFY_INIT','ABHA_VERIFY_CONFIRM','CONSENT_REQUEST','CONSENT_NOTIFY','HI_SHARE')),
  patient_id         uuid REFERENCES patients(id),
  consent_id         uuid REFERENCES patient_consent_records(id),
  identifier_id      uuid REFERENCES patient_identifiers(id),
  related_entity     text,
  related_entity_id  uuid,
  status             text NOT NULL DEFAULT 'INITIATED'
                     CHECK (status IN ('INITIATED','PENDING','SUCCESS','FAILED','RETRY_SCHEDULED','EXPIRED')),
  attempt_count      integer NOT NULL DEFAULT 0,
  max_attempts       integer NOT NULL DEFAULT 3,
  next_retry_at      timestamptz,
  gateway_txn_id     text,
  error_code         text,
  error_message      text,
  request_summary    jsonb,          -- never contains raw identifiers or clinical content
  response_summary   jsonb,
  is_mock            boolean NOT NULL,
  initiated_by       uuid REFERENCES users(id),
  created_at         timestamptz NOT NULL DEFAULT now(),
  updated_at         timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_abdm_txn_patient ON abdm_transactions(patient_id, created_at DESC);
CREATE INDEX ix_abdm_txn_retry ON abdm_transactions(next_retry_at) WHERE status = 'RETRY_SCHEDULED';
CREATE TRIGGER trg_abdm_txn_touch BEFORE UPDATE ON abdm_transactions FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

-- Care contexts: the unit a HIP links to a patient's ABHA so records can be discovered
CREATE TABLE abdm_care_contexts (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  patient_id       uuid NOT NULL REFERENCES patients(id),
  consultation_id  uuid NOT NULL UNIQUE REFERENCES consultations(id),
  reference        text NOT NULL UNIQUE,
  display          text NOT NULL,
  link_status      text NOT NULL DEFAULT 'NOT_LINKED' CHECK (link_status IN ('NOT_LINKED','LINKED','FAILED')),
  is_mock          boolean NOT NULL,
  created_at       timestamptz NOT NULL DEFAULT now()
);
