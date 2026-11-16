// The one Content-Security-Policy for the web client. nginx.conf (Docker) and
// vercel.json (Vercel) carry the same string; tests/deploy-config.test.ts checks
// that they match. The dev server sends it too, so the e2e suites run under it.
// img-src allows blob: because clinical photos and handwritten pages are fetched
// with the user's token and displayed through object URLs.
export const CSP =
  "default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; script-src 'self'; " +
  "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'";

export const SECURITY_HEADERS = {
  "Content-Security-Policy": CSP,
  "X-Content-Type-Options": "nosniff",
  "X-Frame-Options": "DENY",
  "Referrer-Policy": "no-referrer",
  "Permissions-Policy": "camera=(), microphone=(), geolocation=()",
};
