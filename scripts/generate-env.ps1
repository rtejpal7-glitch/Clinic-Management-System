# Creates .env with random secrets for Docker Compose (Windows PowerShell).
# Usage (from the project folder):  powershell -ExecutionPolicy Bypass -File scripts\generate-env.ps1
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
if (Test-Path .env) { Write-Error ".env already exists; refusing to overwrite. Delete it first if you really want new secrets (and run 'docker compose down -v')." }
function Rand64 { $b = New-Object byte[] 32; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); [Convert]::ToBase64String($b) }
function Pw { $chars = [char[]]((48..57)+(65..90)+(97..122)); $b = New-Object byte[] 24; [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); -join ($b | ForEach-Object { $chars[$_ % $chars.Length] }) }
@"
APP_ENV=development
POSTGRES_SUPERUSER_PASSWORD=$(Pw)
CLINICFLOW_OWNER_PASSWORD=$(Pw)
CLINICFLOW_APP_PASSWORD=$(Pw)
DATA_ENCRYPTION_KEY=$(Rand64)
BLIND_INDEX_KEY=$(Rand64)
BOOTSTRAP_ADMIN_PASSWORD=Clinic-Demo-2026
"@ | Set-Content -Encoding ascii .env
Write-Host "Created .env. Admin username: admin   password: Clinic-Demo-2026"
