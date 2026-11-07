-- ClinicFlow V008: least-privilege grants for the runtime application role.
-- Migrations run as the schema owner; the API server connects as clinicflow_app.
-- The role itself is created by db/init (cluster-level object). If it does not
-- exist (e.g. a single-user dev database) this migration is a no-op.
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'clinicflow_app') THEN
    EXECUTE 'GRANT USAGE ON SCHEMA public TO clinicflow_app';
    EXECUTE 'GRANT SELECT, INSERT, UPDATE ON ALL TABLES IN SCHEMA public TO clinicflow_app';
    EXECUTE 'GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA public TO clinicflow_app';
    -- deletes only where the workflow genuinely needs them (replacing draft child rows)
    EXECUTE 'GRANT DELETE ON consultation_diagnoses, prescription_items, investigation_orders,
             doctor_schedules, role_permissions, user_roles TO clinicflow_app';
    -- audit history and payments: append-only for the application
    EXECUTE 'REVOKE UPDATE, DELETE, TRUNCATE ON audit_logs, payments, consultation_addenda FROM clinicflow_app';
    EXECUTE 'REVOKE ALL ON schema_migrations FROM clinicflow_app';
    EXECUTE 'GRANT SELECT ON schema_migrations TO clinicflow_app';
  END IF;
END $$;
