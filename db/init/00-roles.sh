#!/bin/bash
# Creates ClinicFlow database roles inside the official postgres image on first start.
set -euo pipefail
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-SQL
  CREATE ROLE clinicflow_owner LOGIN PASSWORD '${CLINICFLOW_OWNER_PASSWORD}';
  CREATE ROLE clinicflow_app   LOGIN PASSWORD '${CLINICFLOW_APP_PASSWORD}';
  CREATE DATABASE clinicflow OWNER clinicflow_owner;
SQL
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname clinicflow <<-SQL
  REVOKE CREATE ON SCHEMA public FROM PUBLIC;
  ALTER SCHEMA public OWNER TO clinicflow_owner;
  CREATE EXTENSION IF NOT EXISTS pg_trgm;
  CREATE EXTENSION IF NOT EXISTS btree_gist;
SQL
