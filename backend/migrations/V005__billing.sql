-- ClinicFlow V005: billing. Financial records are kept separate from clinical
-- records; bills only reference appointments/consultations by id.
-- All money values are integer paise to avoid floating-point rounding errors.

CREATE TABLE service_catalogue (
  id           uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  code         text NOT NULL UNIQUE CHECK (code ~ '^[A-Z0-9_-]{2,30}$'),
  name         text NOT NULL,
  category     text NOT NULL CHECK (category IN ('CONSULTATION','PROCEDURE','INVESTIGATION','OTHER')),
  price_paise  bigint NOT NULL CHECK (price_paise >= 0),
  tax_rate_bp  integer NOT NULL DEFAULT 0 CHECK (tax_rate_bp BETWEEN 0 AND 2800),  -- basis points (1800 = 18%)
  is_active    boolean NOT NULL DEFAULT true,
  created_at   timestamptz NOT NULL DEFAULT now(),
  updated_at   timestamptz NOT NULL DEFAULT now()
);
CREATE TRIGGER trg_services_touch BEFORE UPDATE ON service_catalogue FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE bills (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  invoice_number   text NOT NULL UNIQUE,
  patient_id       uuid NOT NULL REFERENCES patients(id) ON DELETE RESTRICT,
  appointment_id   uuid REFERENCES appointments(id),
  consultation_id  uuid REFERENCES consultations(id),
  status           text NOT NULL DEFAULT 'PENDING'
                   CHECK (status IN ('PENDING','PARTIALLY_PAID','PAID','REFUNDED','CANCELLED')),
  subtotal_paise   bigint NOT NULL CHECK (subtotal_paise >= 0),
  discount_paise   bigint NOT NULL DEFAULT 0 CHECK (discount_paise >= 0),
  tax_paise        bigint NOT NULL DEFAULT 0 CHECK (tax_paise >= 0),
  total_paise      bigint NOT NULL CHECK (total_paise >= 0),
  paid_paise       bigint NOT NULL DEFAULT 0 CHECK (paid_paise >= 0),
  refunded_paise   bigint NOT NULL DEFAULT 0 CHECK (refunded_paise >= 0),
  discount_reason  text,
  notes            text,
  cancelled_reason text,
  created_by       uuid REFERENCES users(id),
  created_at       timestamptz NOT NULL DEFAULT now(),
  updated_at       timestamptz NOT NULL DEFAULT now(),
  CHECK (discount_paise <= subtotal_paise),
  CHECK (total_paise = subtotal_paise - discount_paise + tax_paise),
  CHECK (refunded_paise <= paid_paise),
  CHECK (paid_paise <= total_paise)
);
CREATE INDEX ix_bills_patient ON bills(patient_id, created_at DESC);
CREATE INDEX ix_bills_created ON bills(created_at DESC);
CREATE INDEX ix_bills_status ON bills(status);
CREATE INDEX ix_bills_appointment ON bills(appointment_id);
CREATE TRIGGER trg_bills_touch BEFORE UPDATE ON bills FOR EACH ROW EXECUTE FUNCTION cf_touch_updated_at();

CREATE TABLE bill_items (
  id                   uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  bill_id              uuid NOT NULL REFERENCES bills(id) ON DELETE RESTRICT,
  service_id           uuid REFERENCES service_catalogue(id),
  description          text NOT NULL,
  quantity             integer NOT NULL CHECK (quantity BETWEEN 1 AND 1000),
  unit_price_paise     bigint NOT NULL CHECK (unit_price_paise >= 0),
  tax_rate_bp          integer NOT NULL DEFAULT 0 CHECK (tax_rate_bp BETWEEN 0 AND 2800),
  line_subtotal_paise  bigint NOT NULL,
  line_discount_paise  bigint NOT NULL DEFAULT 0,
  line_tax_paise       bigint NOT NULL DEFAULT 0,
  line_total_paise     bigint NOT NULL,
  sort_order           integer NOT NULL DEFAULT 0,
  CHECK (line_subtotal_paise = quantity * unit_price_paise),
  CHECK (line_total_paise = line_subtotal_paise - line_discount_paise + line_tax_paise)
);
CREATE INDEX ix_bill_items ON bill_items(bill_id);

CREATE TABLE payments (
  id            uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  bill_id       uuid NOT NULL REFERENCES bills(id) ON DELETE RESTRICT,
  payment_type  text NOT NULL CHECK (payment_type IN ('PAYMENT','REFUND')),
  amount_paise  bigint NOT NULL CHECK (amount_paise > 0),
  method        text NOT NULL CHECK (method IN ('CASH','UPI','CARD','OTHER')),
  reference     text,
  notes         text,
  received_by   uuid REFERENCES users(id),
  received_at   timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX ix_payments_bill ON payments(bill_id);
CREATE INDEX ix_payments_received ON payments(received_at);

-- Payments are financial records: corrections happen through refunds, not edits.
CREATE TRIGGER trg_payments_immutable BEFORE UPDATE OR DELETE ON payments
  FOR EACH ROW EXECUTE FUNCTION cf_audit_immutable();
