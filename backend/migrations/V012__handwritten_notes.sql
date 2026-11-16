-- ClinicFlow V012: handwritten consultation notes (stylus / Apple Pencil) and
-- doctor-reviewed transcriptions.
--
--  * handwritten_note_pages keeps the original ink: vector strokes and a rendered
--    PNG, both encrypted at the application layer like clinical photographs. Pages
--    can be edited while the consultation is a draft and lock with it.
--  * handwriting_transcriptions records each analysis: which recognition provider
--    produced which text, the text after the doctor's corrections, the structured
--    fields the doctor accepted, and who accepted them when. Machine output is
--    never applied to the record without this explicit acceptance.

CREATE TABLE handwritten_note_pages (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id) ON DELETE RESTRICT,
  patient_id       uuid NOT NULL REFERENCES patients(id),
  facility_id      uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  page_no          integer NOT NULL CHECK (page_no BETWEEN 1 AND 20),
  width            integer NOT NULL CHECK (width BETWEEN 100 AND 4000),
  height           integer NOT NULL CHECK (height BETWEEN 100 AND 6000),
  stroke_count     integer NOT NULL CHECK (stroke_count >= 0),
  strokes_enc      text NOT NULL,          -- encrypted JSON vector ink
  image_enc        text,                   -- encrypted PNG rendering (NULL for an empty page)
  image_sha256     text,
  version          integer NOT NULL DEFAULT 1,
  created_by       uuid NOT NULL REFERENCES users(id),
  created_at       timestamptz NOT NULL DEFAULT now(),
  updated_by       uuid NOT NULL REFERENCES users(id),
  updated_at       timestamptz NOT NULL DEFAULT now(),
  UNIQUE (consultation_id, page_no)
);
CREATE TRIGGER trg_hw_pages_lock BEFORE INSERT OR UPDATE OR DELETE ON handwritten_note_pages
  FOR EACH ROW EXECUTE FUNCTION cf_consultation_child_lock();

CREATE TABLE handwriting_transcriptions (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  consultation_id  uuid NOT NULL REFERENCES consultations(id) ON DELETE RESTRICT,
  patient_id       uuid NOT NULL REFERENCES patients(id),
  facility_id      uuid NOT NULL DEFAULT cf_current_facility() REFERENCES facilities(id),
  provider         text NOT NULL,          -- NONE (typed by doctor) | GOOGLE_VISION | HTTP:<name>
  provider_detail  text,                   -- model / endpoint label, error text if recognition failed
  page_versions    jsonb NOT NULL,         -- [{"page":1,"version":3}] - exactly which ink was analysed
  recognized_text  text,                   -- raw machine output (NULL when no provider)
  corrected_text   text,                   -- the doctor's reviewed text
  extraction       jsonb,                  -- structured fields proposed from corrected_text
  accepted_fields  jsonb,                  -- what the doctor accepted and applied
  status           text NOT NULL DEFAULT 'PENDING_REVIEW' CHECK (status IN ('PENDING_REVIEW','ACCEPTED','DISCARDED')),
  created_by       uuid NOT NULL REFERENCES users(id),
  created_at       timestamptz NOT NULL DEFAULT now(),
  reviewed_by      uuid REFERENCES users(id),
  reviewed_at      timestamptz,
  CHECK (status = 'PENDING_REVIEW' OR (reviewed_by IS NOT NULL AND reviewed_at IS NOT NULL)),
  CHECK (status <> 'ACCEPTED' OR (corrected_text IS NOT NULL AND accepted_fields IS NOT NULL))
);
CREATE INDEX ix_hw_tx_consultation ON handwriting_transcriptions(consultation_id, created_at DESC);

-- Reviewed transcriptions are part of the clinical record: immutable once decided.
CREATE OR REPLACE FUNCTION cf_hw_tx_guard() RETURNS trigger AS $$
BEGIN
  IF TG_OP = 'DELETE' THEN
    RAISE EXCEPTION 'transcriptions cannot be deleted' USING ERRCODE = 'P0001';
  END IF;
  IF OLD.status <> 'PENDING_REVIEW' THEN
    RAISE EXCEPTION 'a reviewed transcription is immutable' USING ERRCODE = 'P0001';
  END IF;
  IF NEW.recognized_text IS DISTINCT FROM OLD.recognized_text OR NEW.provider <> OLD.provider
     OR NEW.page_versions <> OLD.page_versions OR NEW.created_by <> OLD.created_by THEN
    RAISE EXCEPTION 'machine output of a transcription cannot be changed' USING ERRCODE = 'P0001';
  END IF;
  RETURN NEW;
END $$ LANGUAGE plpgsql;
CREATE TRIGGER trg_hw_tx_guard BEFORE UPDATE OR DELETE ON handwriting_transcriptions
  FOR EACH ROW EXECUTE FUNCTION cf_hw_tx_guard();

DO $$
DECLARE
  t text;
BEGIN
  FOREACH t IN ARRAY ARRAY['handwritten_note_pages','handwriting_transcriptions'] LOOP
    EXECUTE format('CREATE INDEX %I ON %I(facility_id)', 'ix_' || t || '_facility', t);
    EXECUTE format('ALTER TABLE %I ENABLE ROW LEVEL SECURITY', t);
    EXECUTE format('ALTER TABLE %I FORCE ROW LEVEL SECURITY', t);
    EXECUTE format('CREATE POLICY facility_isolation ON %I USING (cf_rls_bypass() OR facility_id = cf_current_facility()) '
                   'WITH CHECK (cf_rls_bypass() OR facility_id = cf_current_facility())', t);
  END LOOP;
  IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'clinicflow_app') THEN
    EXECUTE 'GRANT SELECT, INSERT, UPDATE ON handwritten_note_pages, handwriting_transcriptions TO clinicflow_app';
    EXECUTE 'GRANT DELETE ON handwritten_note_pages TO clinicflow_app';
  END IF;
END $$;
