-- ClinicFlow V003: doctors, schedules, appointments, queue, notifications

CREATE TABLE doctors (
  id                     uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id                uuid NOT NULL UNIQUE REFERENCES users(id) ON DELETE RESTRICT,
  display_name           text NOT NULL,
  qualification          text,
  specialization         text,
  registration_number    text,     -- State Medical Council / NMC registration
  registration_council   text,
  hpr_id                 text,     -- ABDM Healthcare Professional Registry ID, once onboarded
  consultation_fee_paise bigint NOT NULL DEFAULT 50000 CHECK (consultation_fee_paise >= 0),
  is_active              boolean NOT NULL DEFAULT true,
  created_at             timestamptz NOT NULL DEFAULT now(),
  updated_at             timestamptz NOT NULL DEFAULT now()
);
CREATE TRIGGER trg_doctors_touch BEFORE UPDATE ON doctors FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

-- Weekly working hours. weekday uses ISO numbering: 1 = Monday ... 7 = Sunday.
CREATE TABLE doctor_schedules (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  doctor_id       uuid NOT NULL REFERENCES doctors(id) ON DELETE CASCADE,
  weekday         smallint NOT NULL CHECK (weekday BETWEEN 1 AND 7),
  start_time      time NOT NULL,
  end_time        time NOT NULL,
  slot_minutes    integer NOT NULL CHECK (slot_minutes BETWEEN 5 AND 120),
  is_active       boolean NOT NULL DEFAULT true,
  created_at      timestamptz NOT NULL DEFAULT now(),
  CHECK (end_time > start_time),
  -- no overlapping blocks for the same doctor and weekday
  EXCLUDE USING gist (doctor_id WITH =, weekday WITH =,
                      tsrange(DATE '2000-01-01' + start_time, DATE '2000-01-01' + end_time) WITH &&)
    WHERE (is_active)
);
CREATE INDEX ix_schedules_doctor ON doctor_schedules(doctor_id, weekday);

CREATE TABLE appointments (
  id                       uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  patient_id               uuid NOT NULL REFERENCES patients(id) ON DELETE RESTRICT,
  doctor_id                uuid NOT NULL REFERENCES doctors(id) ON DELETE RESTRICT,
  appointment_type         text NOT NULL CHECK (appointment_type IN ('SCHEDULED','WALK_IN','FOLLOW_UP')),
  start_at                 timestamptz NOT NULL,
  end_at                   timestamptz NOT NULL,
  status                   text NOT NULL DEFAULT 'SCHEDULED'
                           CHECK (status IN ('SCHEDULED','CHECKED_IN','IN_CONSULTATION','COMPLETED','CANCELLED','NO_SHOW')),
  reason                   text,
  notes                    text,
  follow_up_of             uuid REFERENCES appointments(id),
  cancellation_reason      text,
  checked_in_at            timestamptz,
  consultation_started_at  timestamptz,
  completed_at             timestamptz,
  cancelled_at             timestamptz,
  created_by               uuid REFERENCES users(id),
  created_at               timestamptz NOT NULL DEFAULT now(),
  updated_at               timestamptz NOT NULL DEFAULT now(),
  version                  integer NOT NULL DEFAULT 1,
  CHECK (end_at > start_at),
  -- Double-booking prevention enforced by the database itself: no two active
  -- slot-based appointments for the same doctor may overlap in time.
  CONSTRAINT ex_appointments_no_overlap EXCLUDE USING gist (
    doctor_id WITH =, tstzrange(start_at, end_at, '[)') WITH &&
  ) WHERE (appointment_type <> 'WALK_IN' AND status IN ('SCHEDULED','CHECKED_IN','IN_CONSULTATION','COMPLETED'))
);
CREATE INDEX ix_appointments_doctor_start ON appointments(doctor_id, start_at);
CREATE INDEX ix_appointments_patient ON appointments(patient_id, start_at DESC);
CREATE INDEX ix_appointments_start ON appointments(start_at);
CREATE TRIGGER trg_appointments_touch BEFORE UPDATE ON appointments FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE appointment_status_history (
  id              bigserial PRIMARY KEY,
  appointment_id  uuid NOT NULL REFERENCES appointments(id) ON DELETE CASCADE,
  from_status     text,
  to_status       text NOT NULL,
  changed_by      uuid REFERENCES users(id),
  changed_at      timestamptz NOT NULL DEFAULT now(),
  note            text
);
CREATE INDEX ix_appt_history ON appointment_status_history(appointment_id, changed_at);

CREATE TABLE queue_tokens (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  appointment_id  uuid NOT NULL UNIQUE REFERENCES appointments(id) ON DELETE CASCADE,
  doctor_id       uuid NOT NULL REFERENCES doctors(id),
  queue_date      date NOT NULL,
  token_number    integer NOT NULL CHECK (token_number > 0),
  status          text NOT NULL DEFAULT 'WAITING'
                  CHECK (status IN ('WAITING','CALLED','IN_CONSULTATION','DONE','SKIPPED','CANCELLED')),
  issued_at       timestamptz NOT NULL DEFAULT now(),
  called_at       timestamptz,
  completed_at    timestamptz,
  UNIQUE (doctor_id, queue_date, token_number)
);
CREATE INDEX ix_queue_date ON queue_tokens(queue_date, doctor_id, status);

-- Notification outbox (reminders). Delivery happens through a pluggable channel;
-- the PoC ships only a logging channel that records what would be sent.
CREATE TABLE notifications (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  channel         text NOT NULL CHECK (channel IN ('SMS','EMAIL','WHATSAPP')),
  recipient       text NOT NULL,
  template_code   text NOT NULL,
  payload         jsonb NOT NULL DEFAULT '{}'::jsonb,
  entity_type     text,
  entity_id       uuid,
  scheduled_for   timestamptz NOT NULL,
  status          text NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING','SENT','FAILED','CANCELLED')),
  attempts        integer NOT NULL DEFAULT 0,
  last_error      text,
  provider        text,
  sent_at         timestamptz,
  created_at      timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_notifications_due ON notifications(scheduled_for) WHERE status = 'PENDING';
CREATE INDEX ix_notifications_entity ON notifications(entity_type, entity_id);
