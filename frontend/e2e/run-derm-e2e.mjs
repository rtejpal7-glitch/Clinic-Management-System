// End-to-end UI tests for the dermatology module (Playwright, Chromium). Needs a
// running stack seeded with scripts/seed_demo.py and scripts/seed_derm_demo.py.
//   E2E_BASE_URL=http://localhost:5173 node e2e/run-derm-e2e.mjs
import { chromium } from "playwright";
import { mkdirSync } from "node:fs";
import { deflateSync } from "node:zlib";
import assert from "node:assert/strict";

const BASE = process.env.E2E_BASE_URL || "http://localhost:5173";
const PASS = process.env.E2E_PASSWORD || "Clinic-Demo-2026";
const SHOTS = process.env.E2E_SCREENSHOTS || "e2e/screenshots";
mkdirSync(SHOTS, { recursive: true });

const results = [];
let page;
async function test(name, fn) {
  try {
    await fn();
    results.push([name, true]);
    console.log(`  PASS  ${name}`);
  } catch (e) {
    results.push([name, false]);
    console.log(`  FAIL  ${name}\n        ${e.message.split("\n")[0]}`);
    await page.screenshot({ path: `${SHOTS}/FAILED-derm-${name.replace(/[^a-z0-9]+/gi, "-")}.png`, fullPage: true }).catch(() => {});
  }
}

// --- API helpers (test set-up only) ---
async function api(token, method, path, body) {
  const r = await fetch(`${BASE}/api/v1${path}`, { method, headers: { "Content-Type": "application/json", ...(token ? { Authorization: `Bearer ${token}` } : {}) },
    body: body ? JSON.stringify(body) : undefined });
  const t = await r.text();
  return { status: r.status, body: t ? JSON.parse(t) : null };
}
const token = async (u) => (await api(null, "POST", "/auth/login", { username: u, password: PASS })).body.token;

// A small synthetic PNG (solid skin-tone rectangle) for the photo upload test.
function png(w = 64, h = 48) {
  const crcTable = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = (buf) => { let c = 0xffffffff; for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const o = y * (w * 3 + 1) + 1 + x * 3; raw[o] = 214; raw[o + 1] = 160 - (x % 9); raw[o + 2] = 130; }
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}

const SUFFIX = Array.from({ length: 5 }, () => "abcdefghijklmnopqrstuvwxyz"[Math.floor(Math.random() * 26)]).join("");
const PATIENT = `Rekha Derm${SUFFIX.charAt(0).toUpperCase() + SUFFIX.slice(1)}`;

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, locale: "en-IN", timezoneId: "Asia/Kolkata" });
page = await ctx.newPage();
const consoleErrors = [];
page.on("console", (m) => m.type() === "error" && !/Failed to load resource/.test(m.text()) && consoleErrors.push(m.text()));
page.on("pageerror", (e) => consoleErrors.push(e.message));
const shot = (n, full = true) => page.screenshot({ path: `${SHOTS}/${n}.png`, fullPage: full });
async function login(user) {
  await page.goto(BASE + "/#/");
  if (await page.getByRole("button", { name: "Sign out" }).isVisible().catch(() => false)) await page.getByRole("button", { name: "Sign out" }).click();
  await page.getByLabel("Username").fill(user);
  await page.getByLabel("Password").fill(PASS);
  await page.getByRole("button", { name: "Sign in" }).click();
  await page.locator(".sidebar").waitFor();
}

// Set-up: a waiting urticaria patient for dr.kavya
const rec = await token("reception");
const kavyaTok = await token("dr.kavya");
const me = (await api(kavyaTok, "GET", "/auth/me")).body;
const pt = (await api(rec, "POST", "/patients", { fullName: PATIENT, dateOfBirth: "1994-06-15", gender: "FEMALE", mobile: "97" + String(Math.floor(Math.random() * 1e8)).padStart(8, "0"),
  city: "Bengaluru", state: "Karnataka", pincode: "560041" })).body;
assert.ok(pt.id, "patient created");
const walk = await api(rec, "POST", "/appointments/walk-in", { patientId: pt.id, doctorId: me.doctorId, reason: "Itchy wheals" });
assert.equal(walk.status, 201);

await test("dermatologist starts a consultation and records the structured assessment", async () => {
  await login("dr.kavya");
  await page.goto(BASE + "/#/queue");
  await page.locator("tr", { hasText: PATIENT }).first().getByRole("button", { name: "Start consultation" }).click();
  await page.getByLabel("Chief complaint").fill("Itchy raised wheals on arms and back for five days");
  await page.getByLabel("Add coded diagnosis (ICD-10)").fill("urticaria");
  await page.locator(".combo-list li", { hasText: "L50.9" }).first().click();
  await page.getByRole("button", { name: "Save draft" }).click();
  await page.getByText("Draft saved").waitFor();
  await page.getByRole("tab", { name: "Dermatology assessment" }).click();
  await page.getByLabel("Type of skin problem").selectOption("HIVES_SWELLING");
  await page.locator("#dur").fill("5");
  await page.getByLabel("Duration unit").selectOption("DAYS");
  await page.getByLabel("Severity (clinician)").selectOption("MODERATE");
  const chip = (group, label) => page.getByRole("group", { name: group }).getByRole("button", { name: label, exact: true });
  await chip("Symptoms", "Itching").click();
  await chip("Body location", "Arms").click();
  await chip("Body location", "Back").click();
  await chip("Lesion type", "Wheal (hive)").click();
  await page.getByLabel("Distribution").selectOption("GENERALISED");
  await page.getByLabel("Pregnancy / breastfeeding").selectOption("NOT_PREGNANT");
  await page.getByRole("button", { name: "Save assessment" }).click();
  await page.getByText("Dermatology assessment saved").waitFor();
  await shot("30-derm-assessment");
});

await test("clinical photograph requires consent and is shown from the encrypted store", async () => {
  await page.getByRole("button", { name: "Add photograph" }).click();
  const dlg = page.getByRole("dialog", { name: "Add clinical photograph" });
  assert.equal(await dlg.getByRole("button", { name: "Save photograph" }).isDisabled(), true);
  await dlg.locator(".consent-box input").check();
  await dlg.locator('input[type="file"]').setInputFiles({ name: "forearm.png", mimeType: "image/png", buffer: png() });
  await dlg.getByLabel("Body site").selectOption("ARMS");
  await dlg.getByLabel("Caption").fill("Left forearm, day 5");
  await dlg.getByRole("button", { name: "Save photograph" }).click();
  await page.getByText("Photograph saved").waitFor();
  await page.locator(".photo-thumb img").first().waitFor();
  await shot("31-derm-photo");
});

await test("decision support shows three separate panels side by side", async () => {
  await page.getByRole("tab", { name: /Decision support/ }).click();
  const a = page.getByRole("region", { name: "Clinical knowledge" });
  const b = page.getByRole("region", { name: "My prescribing patterns" });
  const c = page.getByRole("region", { name: "ClinicFlow network" });
  await a.getByText(/Urticaria|No approved clinical content/).first().waitFor();
  await b.locator(".cds-state, .pattern-table").first().waitFor();
  await c.locator(".cds-state, .pattern-table").first().waitFor();
  const boxes = await Promise.all([a, b, c].map((x) => x.boundingBox()));
  assert.ok(boxes[0].y === boxes[1].y && boxes[1].y === boxes[2].y, "panels on one row at desktop width");
  assert.equal(await page.locator(".rx-summary").getByText("nothing yet").count(), 1, "nothing auto-prescribed");
  await shot("32-decision-support");
});

await test("doctor adds a network suggestion, modifies it, and sees the safety check", async () => {
  const c = page.getByRole("region", { name: "ClinicFlow network" });
  if (await c.locator(".pattern-table").count()) {
    await c.locator(".pattern-table tbody tr").first().getByRole("button", { name: "Add" }).click();
  } else {
    const a = page.getByRole("region", { name: "Clinical knowledge" });
    await a.getByRole("button", { name: "Add" }).first().click();
  }
  await page.getByText(/added to the prescription/).waitFor();
  await page.getByRole("tab", { name: /Prescription & orders/ }).click();
  await page.locator(".src-tag").first().waitFor();
  await page.getByLabel("Frequency").first().fill("Twice daily");
  await page.getByRole("button", { name: "Save draft" }).click();
  await page.getByText("Draft saved").waitFor();
  await page.getByText("Prescription safety check").waitFor();
  await page.getByText(/modified/).first().waitFor();
  await shot("33-prescription-with-source");
});

await test("finalize records decisions; outcome can be recorded", async () => {
  await page.getByRole("button", { name: "Finalize", exact: true }).click();
  await page.getByRole("button", { name: "Finalize and lock" }).click();
  await page.getByText(/This record is locked/).waitFor();
  await page.getByRole("tab", { name: "Decisions & outcome" }).click();
  await page.getByText("Suggestions vs final prescription").waitFor();
  await page.locator("table.data").getByText("Modified").first().waitFor();
  await page.getByLabel("Outcome at follow-up").selectOption("IMPROVED");
  await page.getByLabel("Notes").fill("Fewer wheals at review");
  await page.getByRole("button", { name: "Record outcome" }).click();
  await page.getByText("Outcome recorded").waitFor();
  await shot("34-decisions-outcome");
});

await test("practice insights: own analytics separate from the network", async () => {
  await page.goto(BASE + "/#/insights");
  await page.getByText("Frequently prescribed (90 days)").waitFor();
  await shot("35-insights-mine");
  await page.getByRole("tab", { name: "ClinicFlow network" }).click();
  await page.getByText("Diagnoses with enough network data").waitFor();
  await shot("36-insights-network");
});

await test("clinical content review (reviewer)", async () => {
  await login("reviewer.demo");
  await page.goto(BASE + "/#/clinical-content");
  await page.getByText(/awaiting clinical review/).waitFor();
  await page.getByRole("button", { name: "Approved" }).click();
  await page.getByText("AI-drafted starter content").first().waitFor();
  await shot("37-clinical-content");
  assert.equal(await page.getByRole("link", { name: "Patient directory" }).count(), 0, "reviewer has no patient access");
});

await test("clinic & network settings (platform admin)", async () => {
  await login("admin");
  await page.goto(BASE + "/#/clinics");
  await page.getByText("ClinicFlow network participation").waitFor();
  await page.getByText("Clinics on this installation").waitFor();
  await shot("38-clinics-network");
});

await test("tablet layout of the decision-support screen", async () => {
  await login("dr.kavya");
  await page.setViewportSize({ width: 1024, height: 768 });
  const tok = await token("dr.kavya");
  const q = (await api(tok, "GET", "/queue")).body.filter((t) => t.doctorId === me.doctorId && t.status === "WAITING");
  if (q.length) {
    const c = (await api(tok, "POST", `/appointments/${q[0].appointmentId}/start-consultation`)).body;
    await page.goto(BASE + `/#/consultations/${c.id}`);
    await page.getByRole("tab", { name: /Decision support/ }).click();
    await page.getByRole("region", { name: "Clinical knowledge" }).locator(".cds-state, .cond-list").first().waitFor();
    await shot("39-decision-support-tablet");
  }
  await page.setViewportSize({ width: 1440, height: 900 });
});

await test("no uncaught browser errors", async () => {
  assert.deepEqual(consoleErrors, []);
});

await browser.close();
const failed = results.filter((r) => !r[1]).length;
console.log(`\n${results.length - failed} passed, ${failed} failed. Screenshots in ${SHOTS}/`);
process.exit(failed ? 1 : 0);
