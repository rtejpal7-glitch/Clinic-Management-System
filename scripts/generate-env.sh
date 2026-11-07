#!/usr/bin/env bash
# Creates .env with random database passwords and encryption keys.
# Never commit .env. Back up DATA_ENCRYPTION_KEY and BLIND_INDEX_KEY securely:
# losing them makes encrypted ABHA identifiers unreadable.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -f .env ]; then echo ".env already exists; refusing to overwrite." >&2; exit 1; fi
rand() { openssl rand -base64 "$1" | tr -d '\n'; }
pw() { openssl rand -base64 24 | tr -dc 'A-Za-z0-9' | head -c 24; }
cat > .env <<EOF
APP_ENV=development
POSTGRES_SUPERUSER_PASSWORD=$(pw)
CLINICFLOW_OWNER_PASSWORD=$(pw)
CLINICFLOW_APP_PASSWORD=$(pw)
DATA_ENCRYPTION_KEY=$(rand 32)
BLIND_INDEX_KEY=$(rand 32)
BOOTSTRAP_ADMIN_PASSWORD=Clinic-$(pw | head -c 10)-1
EOF
chmod 600 .env
echo "Created .env. Initial admin username: admin, password: $(grep BOOTSTRAP_ADMIN_PASSWORD .env | cut -d= -f2)"
