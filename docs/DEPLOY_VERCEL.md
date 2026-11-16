# Hosting the web client on Vercel

Vercel can host the **web client only** (the React app in `frontend/`). The C++ API server and PostgreSQL cannot run on Vercel: they need a long-running process and a database server. The supported layout is:

```
iPad Safari ──HTTPS──▶ Vercel (static app + /api rewrite) ──HTTPS──▶ your ClinicFlow backend ──▶ PostgreSQL
                       clinic.example.com                           api.clinic.example.com
```

Because Vercel forwards `/api/*` to the backend, the browser sees one origin. No CORS configuration is needed, and the Content-Security-Policy can stay at `connect-src 'self'`.

## 1. Host the backend and database

**Simplest:** follow [DEPLOY_RENDER.md](DEPLOY_RENDER.md). The repository's `render.yaml` sets up the API and PostgreSQL on Render, and the steps there cover Vercel too. The rest of this section is for hosting the backend somewhere else.

Run the `backend` and `db` services from `docker-compose.yml` on any server or container platform (a VM, AWS ECS/Lightsail, Azure Container Apps, Google Cloud Run with Cloud SQL, Railway, Render…). For patient data, choose a region in India where possible and keep the database private.

Requirements:
- The backend must be reachable over **HTTPS** with a valid certificate (put Caddy, nginx or the platform's load balancer in front of port 8000).
- Allow request bodies up to **10 MB** on that proxy: clinical photos and handwritten pages are uploaded as JSON (the backend itself enforces 9 MB for those endpoints and 1 MB elsewhere).
- Allow at least **90 s** read timeout if handwriting recognition is enabled.
- Set `TRUST_PROXY_HEADERS=true` so audit logs record the visitor's address from `X-Forwarded-For`, which Vercel sets.
- Set the same `.env` values as for Docker (`scripts/generate-env.sh` or `.ps1`), plus `HWR_*` if you use recognition (see `HANDWRITING.md`). On a hosted PostgreSQL without a superuser, set `MIGRATION_DATABASE_URL` to the provider's login and `APP_DB_PASSWORD` instead of `DATABASE_URL`; the API then creates its own least-privilege role (see DEPLOY_RENDER.md).

**Recommended:** accept API traffic only from Vercel. Vercel can add a secret header to proxied requests (see Vercel's "Restricting your origin to Vercel traffic"), and the proxy in front of the backend rejects requests without it. ClinicFlow's backend does not check this header itself.

## 2. Point the rewrite at your backend

Edit `frontend/vercel.json` and replace `https://clinicflow-api.example.invalid` with your backend's HTTPS address, for example `https://api.clinic.example.com`. Paths stay the same (`/api/v1/...`).

The build refuses to run on Vercel while the placeholder is still there, so a half-configured deployment can't go live.

The file also:
- serves `dist/` and falls back to `index.html` for app routes;
- sends the same security headers as the Docker nginx (checked by `frontend/tests/deploy-config.test.ts`), including `img-src blob:` for photos and handwriting, plus HSTS;
- turns off Vercel's CDN caching of API responses (`x-vercel-enable-rewrite-caching: 0`); the backend also sends `Cache-Control: no-store`.

## 3. Create the Vercel project

1. Import the GitHub repository in Vercel.
2. **Root Directory:** `frontend`. Framework preset: **Other**. Build, install and output settings come from `vercel.json` (`node build.mjs`, output `dist`).
3. Node.js version 22 or later (Project Settings → Build and Deployment).
4. Deploy, then add your domain.

No environment variables are needed on Vercel: the app holds no secrets, and all keys stay on the backend.

## 4. Check on the iPad

Open the Vercel URL in Safari on the iPad, sign in as a doctor, start a consultation, and work through the manual checklist in `HANDWRITING.md`. Then **Share → Add to Home Screen** for a full-screen app.

## Notes

- Sign-in tokens are kept in the tab's session storage (see `SECURITY.md`), so they are not shared between Safari tabs or the Home Screen app.
- Request body limits on Vercel's proxy for external rewrites were not verified for this PoC. A handwritten page is usually far below 1 MB; very large clinical photos are the most likely to hit a platform limit. If uploads fail through Vercel but work against the backend directly, serve the API from its own domain instead (that needs CORS and a `connect-src` change).
