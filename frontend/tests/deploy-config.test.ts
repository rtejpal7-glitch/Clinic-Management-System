// Deployment configuration: Docker (nginx) and Vercel must serve the same
// security headers, and both must let handwritten pages and photos through.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
// @ts-ignore - plain ES module shared with the dev server
import { CSP, SECURITY_HEADERS } from "../security-headers.mjs";

const nginx = readFileSync(new URL("../nginx.conf", import.meta.url), "utf8");
const vercel = JSON.parse(readFileSync(new URL("../vercel.json", import.meta.url), "utf8"));

test("nginx and Vercel send the same Content-Security-Policy, which allows blob: images", () => {
  assert.match(CSP, /img-src 'self' data: blob:/);
  assert.ok(nginx.includes(`set $csp "${CSP}";`), "nginx.conf CSP differs from security-headers.mjs");
  const all = vercel.headers.find((h: any) => h.source === "/(.*)").headers;
  const byKey = Object.fromEntries(all.map((h: any) => [h.key, h.value]));
  for (const [k, v] of Object.entries(SECURITY_HEADERS)) assert.equal(byKey[k], v, `vercel.json ${k}`);
});

test("nginx repeats the security headers in every location that adds its own headers", () => {
  const blocks = nginx.split(/\n\s*location /).slice(1);
  for (const b of blocks) {
    const body = b.slice(0, b.indexOf("\n  }"));
    if (body.includes("add_header")) assert.ok(body.includes("Content-Security-Policy $csp"), `location ${body.split("\n")[0]}`);
  }
});

test("API body limits allow 9 MB handwriting pages and photos through nginx", () => {
  const api = nginx.slice(nginx.indexOf("location /api/"), nginx.indexOf("}", nginx.indexOf("location /api/")));
  assert.match(api, /client_max_body_size 10m;/);
});

test("Vercel proxies the API without caching and falls back to the SPA", () => {
  const api = vercel.rewrites.find((r: any) => r.source === "/api/:path*");
  assert.ok(api && /^https:\/\//.test(api.destination) && api.destination.endsWith("/api/:path*"));
  const apiHeaders = vercel.headers.find((h: any) => h.source === "/api/:path*").headers;
  assert.ok(apiHeaders.some((h: any) => h.key === "x-vercel-enable-rewrite-caching" && h.value === "0"));
  assert.equal(vercel.rewrites.at(-1).destination, "/index.html");
  assert.equal(vercel.outputDirectory, "dist");
});
