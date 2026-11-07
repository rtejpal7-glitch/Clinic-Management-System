# Installation and setup

## Option A — Docker Compose (recommended)

Requirements: Docker 24+ with Compose v2, `openssl`, Python 3.9+ (only for the seed script).

```bash
./scripts/generate-env.sh
docker compose up --build
```

The first build compiles Oat++ and the server from source (about 5–10 minutes). Then:

- Web app: http://localhost:8080 (sign in as `admin` with the password printed by `generate-env.sh`; you'll be asked to change it if `APP_ENV=production`)
- Swagger UI: http://localhost:8080/swagger/ui
- API health: http://localhost:8080/api/v1/health

Load synthetic demo data (optional):

```bash
python3 scripts/seed_demo.py --api http://localhost:8080/api/v1 --admin-pass "<BOOTSTRAP_ADMIN_PASSWORD>"
```

On first start the database container runs `db/init/00-roles.sh`. That creates the `clinicflow` database, a schema-owner role (`clinicflow_owner`, used only for migrations) and a least-privilege runtime role (`clinicflow_app`). The API applies migrations automatically at startup.

Reset everything: `docker compose down -v`.

## Option B — Native build (Linux / macOS)

### 1. Dependencies

| Component | Version used | Notes |
|---|---|---|
| CMake, C++17 compiler | CMake 3.20+, GCC 11+/Clang 14+ | |
| PostgreSQL | 16 (13+ works) | needs `pg_trgm`, `btree_gist` (contrib) |
| libpq, OpenSSL | dev packages | `libpq-dev libssl-dev` on Debian/Ubuntu |
| Oat++, oatpp-swagger | 1.3.0 | built from source (below) |
| libargon2 | 20190702 | reference implementation |
| nlohmann/json | 3.11.3 | header only |
| Node.js | 20+ | frontend build |

```bash
PREFIX=$HOME/clinicflow-deps
git clone --depth 1 -b 1.3.0 https://github.com/oatpp/oatpp.git
cmake -S oatpp -B oatpp/build -DOATPP_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$PREFIX && cmake --build oatpp/build -j && cmake --install oatpp/build
git clone --depth 1 -b 1.3.0 https://github.com/oatpp/oatpp-swagger.git
cmake -S oatpp-swagger -B oatpp-swagger/build -DOATPP_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=$PREFIX -DCMAKE_INSTALL_PREFIX=$PREFIX && cmake --build oatpp-swagger/build -j && cmake --install oatpp-swagger/build
git clone --depth 1 -b 20190702 https://github.com/P-H-C/phc-winner-argon2.git && make -C phc-winner-argon2 libs
cp phc-winner-argon2/libargon2.a $PREFIX/lib/ && cp phc-winner-argon2/include/argon2.h $PREFIX/include/
git clone --depth 1 -b v3.11.3 https://github.com/nlohmann/json.git && cp -r json/single_include/nlohmann $PREFIX/include/
```

### 2. Database

```sql
-- as the postgres superuser
CREATE ROLE clinicflow_owner LOGIN PASSWORD '...';
CREATE ROLE clinicflow_app   LOGIN PASSWORD '...';
CREATE DATABASE clinicflow OWNER clinicflow_owner;
\c clinicflow
REVOKE CREATE ON SCHEMA public FROM PUBLIC;
ALTER SCHEMA public OWNER TO clinicflow_owner;
CREATE EXTENSION pg_trgm;
CREATE EXTENSION btree_gist;
```

### 3. Backend

```bash
cd backend
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/clinicflow-deps
cmake --build build -j
ctest --test-dir build            # unit tests

export DATABASE_URL=postgresql://clinicflow_app:...@localhost:5432/clinicflow
export MIGRATION_DATABASE_URL=postgresql://clinicflow_owner:...@localhost:5432/clinicflow
export MIGRATIONS_DIR=$PWD/migrations
export DATA_ENCRYPTION_KEY=$(openssl rand -base64 32)   # store these two keys safely
export BLIND_INDEX_KEY=$(openssl rand -base64 32)
export BOOTSTRAP_ADMIN_PASSWORD='Clinic-Demo-2026'
export SWAGGER_RES_PATH=/path/to/oatpp-swagger/res
./build/clinicflow-server
```

### 4. Frontend

```bash
cd frontend
npm install
npm run dev        # http://localhost:5173, proxies /api to localhost:8000
# production bundle: npm run build  → dist/
```

## Configuration reference (backend environment)

| Variable | Default | Purpose |
|---|---|---|
| `DATABASE_URL` | required | Runtime connection (least-privilege role) |
| `MIGRATION_DATABASE_URL` | = `DATABASE_URL` | Schema owner connection for migrations |
| `DATA_ENCRYPTION_KEY` | required | Base64 32-byte AES-256-GCM key for field encryption |
| `BLIND_INDEX_KEY` | required | Base64 32+ byte HMAC key for searchable encrypted fields (must differ from the encryption key) |
| `BOOTSTRAP_ADMIN_USERNAME` / `_PASSWORD` | `admin` / — | Creates the first administrator when no users exist |
| `APP_ENV` | `development` | In `production` the bootstrap admin must change the password at first sign-in |
| `PORT`, `HOST` | `8000`, `0.0.0.0` | Listener |
| `DB_POOL_SIZE` | `8` | Connection pool size |
| `MIGRATIONS_DIR` | `./migrations` | SQL migrations |
| `RUN_MIGRATIONS` | `true` | Apply pending migrations at startup |
| `SESSION_ABSOLUTE_HOURS` | `12` | Maximum session lifetime (idle timeout is a clinic setting) |
| `TRUST_PROXY_HEADERS` | `false` | Use `X-Forwarded-For` for audit IPs (enable only behind your own proxy) |
| `ABDM_MODE` | `mock` | Only `mock` is implemented; other values refuse to start |
| `SWAGGER_RES_PATH` | — | Path to oatpp-swagger `res/` to enable Swagger UI |
| `WORKER_INTERVAL_SECONDS` | `30` | Background worker cadence |

Clinic-level settings (clinic profile, ID prefixes, idle timeout, duplicate-matching rules) live in the database and are edited under **Clinic settings** in the app.
