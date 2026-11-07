# ClinicFlow — Mini Clinic Management System (PoC)

A working proof of concept of an outpatient clinic management system for a single- or small multi-doctor clinic in India. It uses the **NABH Digital Health Standards for Clinic Management Systems (1st Ed., Sept 2025)** as its design reference, and is architected for future **ABDM** integration.

> **Status:** proof of concept with synthetic data only. ClinicFlow is **not** NABH-certified or ABDM-certified, and implementing these features does not make a clinic or product compliant. ABDM functionality runs against a clearly labelled **mock adapter**: no data is exchanged with ABDM.

## What it does

| Area | Capabilities |
|---|---|
| Patients | Registration with internal immutable ID (`CF-2026-000123`), configurable duplicate detection (fuzzy name, DOB, mobile), search by name / mobile / patient ID / ABHA, demographic change history, allergies |
| Scheduling | Doctor working hours and slot lengths, slot picker, double-booking prevented by the database, reschedule / cancel / no-show, follow-ups, walk-ins, per-doctor queue tokens, "now serving" board, reminder outbox |
| OPD / EMR | Structured notes, 8 vital signs with BMI, ICD-10 coded diagnoses, draft → finalized (locked) records, addenda for corrections, patient timeline, printable visit summary |
| e-Prescription & CPOE | Medicine catalogue, dose / route / frequency / duration, investigation orders with LOINC codes, advice, printable prescription. *No automated clinical decision support (stated in the UI).* |
| Billing | Service rates, multi-line invoices, discounts with reason, GST after discount, integer-paise arithmetic, partial payments, refunds, cancellation, daily summary |
| Security | Argon2id passwords, lockout, forced change of temporary passwords, server-side sessions with idle timeout, RBAC deny-by-default (29 permissions, custom roles), append-only hash-chained audit log, AES-256-GCM field encryption for ABHA, least-privilege DB role |
| ABDM readiness | ABHA linking (OTP flow), consent requests and lifecycle, FHIR R4 document bundles (OPConsultRecord-style), transaction log with retries — all through a swappable `AbdmGateway` port |

## Stack

React 19 + TypeScript (esbuild) · C++17 + Oat++ 1.3 (REST, Swagger) · PostgreSQL 16 (libpq) · Docker Compose. It's a modular monolith: one API process with independent modules.

## Quick start (Docker)

```bash
./scripts/generate-env.sh          # creates .env with random secrets and an admin password
docker compose up --build          # PostgreSQL + API + web client
# Web app:  http://localhost:8080        Swagger UI: http://localhost:8080/swagger/ui
python3 scripts/seed_demo.py --api http://localhost:8080/api/v1 --admin-pass "<admin password from .env>"
```

Demo accounts created by the seed script (password `Clinic-Demo-2026`): `dr.meera`, `dr.arjun` (doctors) and `reception`. For running without Docker, see [docs/SETUP.md](docs/SETUP.md).

## Tests

| Suite | Count | Command |
|---|---|---|
| Backend unit (C++) | 19 | `ctest --test-dir backend/build` |
| API integration (pytest, real DB) | 78 | `pytest tests/api` |
| Frontend unit | 11 | `cd frontend && npm test` |
| End-to-end UI (Playwright) | 20 | `cd frontend && npm run test:e2e` |

See [docs/TESTING.md](docs/TESTING.md).

## Documentation

| Document | Contents |
|---|---|
| [docs/SETUP.md](docs/SETUP.md) | Installation, configuration, running locally with or without Docker |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Architecture diagram, modules, key design decisions |
| [docs/DATABASE.md](docs/DATABASE.md) | Schema, ER diagram, constraints, retention |
| [docs/API.md](docs/API.md) + [docs/api/openapi.json](docs/api/openapi.json) | REST API reference (84 operations) |
| [docs/SECURITY.md](docs/SECURITY.md) | Security controls and how they are implemented |
| [docs/ABDM_INTEGRATION.md](docs/ABDM_INTEGRATION.md) | Interoperability architecture, mock workflows, path to live integration |
| [docs/NABH_TRACEABILITY.md](docs/NABH_TRACEABILITY.md) | Requirement traceability matrix (43 objective elements, verified against the standard) |
| [docs/TESTING.md](docs/TESTING.md) | Test strategy and execution guide |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Known limitations and future development |

## Repository layout

```
backend/        C++ API server (src/, migrations/, test/, Dockerfile)
frontend/       React + TypeScript client (src/, tests/, e2e/, Dockerfile, nginx.conf)
db/init/        PostgreSQL role bootstrap for Docker
scripts/        generate-env.sh, seed_demo.py
tests/api/      API integration tests (pytest)
docs/           documentation, diagrams, OpenAPI spec, screenshots
```
