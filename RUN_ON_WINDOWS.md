# Running ClinicFlow on Windows (VS Code + Docker Desktop)

## Once: prerequisites
- **Docker Desktop**, installed and running (whale icon says *Engine running*).
- **Python 3**, only for loading demo data (`python --version` or `py --version`).

## First run
Open the `clinicflow` folder in VS Code, then **Terminal → New Terminal** (PowerShell).

```powershell
# 1. Remove any database left by an earlier ClinicFlow version (deletes old demo data)
docker compose down -v

# 2. Create the settings file (only if .env does not exist yet)
powershell -ExecutionPolicy Bypass -File scripts\generate-env.ps1

# 3. Build and start (first build: 10-20 minutes). Leave this terminal running.
docker compose up --build
```

Ready when the log shows `Listening on port 8000` and stays up. Open **http://localhost:8080** and sign in as `admin` / `Clinic-Demo-2026`.

## Optional: demo data (second terminal, use `py` if `python` is not found)
```powershell
python scripts\seed_demo.py --api http://localhost:8080/api/v1
python scripts\seed_derm_demo.py --api http://localhost:8080/api/v1 --approve-content-for-demo
```
All demo accounts use `Clinic-Demo-2026`: `dr.kavya` (dermatology, 3 patients waiting), `dr.farhan`, `reception`, `reviewer.demo`, `admin.twacha`, `admin.nirmal`, `dr.meera`.
`--approve-content-for-demo` marks the AI-drafted starter content approved **for demonstration only — not a clinical review**.

## Everyday
| Action | Command |
|---|---|
| Start | `docker compose up` |
| Stop (keeps data) | Ctrl+C, then `docker compose down` |
| Reset everything | `docker compose down -v` (deletes all data; reseed afterwards) |

## Troubleshooting
| Message | Fix |
|---|---|
| `password authentication failed for user "clinicflow_owner"` | `.env` does not match the existing database: `docker compose down -v`, then `docker compose up --build` |
| `.env already exists` | Keep it, or delete it **and** run `docker compose down -v` before generating a new one |
| Port 8080 or 8000 already in use | Stop the other program, or change the left-hand port in `docker-compose.yml` (e.g. `"8081:8080"`) |
| `python` not recognised | Use `py`, or install Python from python.org (tick *Add to PATH*) |
| Anything else | `docker compose logs backend --tail 60` and share the output |
