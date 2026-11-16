// End-to-end test of handwritten consultation notes on an emulated iPad
// (1180x820 landscape, touch, device pixel ratio 2) with synthetic Apple Pencil
// input (PointerEvents with pointerType "pen" and pressure). Chromium engine:
// real Safari/WebKit on an iPad must still be checked by hand (see docs/HANDWRITING.md).
//   E2E_BASE_URL=http://localhost:5173 node e2e/run-handwriting-e2e.mjs
import { chromium } from "playwright";
import { mkdirSync } from "node:fs";
import assert from "node:assert/strict";

const BASE = process.env.E2E_BASE_URL || "http://localhost:5173";
const PASS = process.env.E2E_PASSWORD || "Clinic-Demo-2026";
const SHOTS = process.env.E2E_SCREENSHOTS || "e2e/screenshots";
mkdirSync(SHOTS, { recursive: true });

const results = [];
let page;
async function test(name, fn) {
  try { await fn(); results.push([name, true]); console.log(`  PASS  ${name}`); }
  catch (e) {
    results.push([name, false]);
    console.log(`  FAIL  ${name}\n        ${e.message.slice(0, 600)}`);
    await page.screenshot({ path: `${SHOTS}/FAILED-hw-${name.replace(/[^a-z0-9]+/gi, "-")}.png`, fullPage: true }).catch(() => {});
  }
}
async function api(token, method, path, body) {
  const r = await fetch(`${BASE}/api/v1${path}`, { method, headers: { "Content-Type": "application/json", ...(token ? { Authorization: `Bearer ${token}` } : {}) },
    body: body ? JSON.stringify(body) : undefined });
  const t = await r.text();
  return { status: r.status, body: t ? JSON.parse(t) : null };
}
const token = async (u) => (await api(null, "POST", "/auth/login", { username: u, password: PASS })).body.token;

const SUFFIX = Array.from({ length: 5 }, () => "abcdefghijklmnopqrstuvwxyz"[Math.floor(Math.random() * 26)]).join("");
const PATIENT = `Ananya Ink${SUFFIX.charAt(0).toUpperCase() + SUFFIX.slice(1)}`;
const rec = await token("reception");
const docTok = await token("dr.kavya");
const me = (await api(docTok, "GET", "/auth/me")).body;
const pt = (await api(rec, "POST", "/patients", { fullName: PATIENT, dateOfBirth: "1990-02-11", gender: "FEMALE",
  mobile: "96" + String(Math.floor(Math.random() * 1e8)).padStart(8, "0"), city: "Bengaluru", state: "Karnataka", pincode: "560041" })).body;
assert.ok(pt.id);
assert.equal((await api(rec, "POST", "/appointments/walk-in", { patientId: pt.id, doctorId: me.doctorId, reason: "Itchy rash" })).status, 201);

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1180, height: 820 }, deviceScaleFactor: 2, hasTouch: true, isMobile: false,
  locale: "en-IN", timezoneId: "Asia/Kolkata" });
// iPadOS Safari reports navigator.maxTouchPoints = 5; Chromium's touch emulation reports 1.
await ctx.addInitScript(() => Object.defineProperty(Navigator.prototype, "maxTouchPoints", { get: () => 5 }));
page = await ctx.newPage();
const consoleErrors = [];
page.on("console", (m) => m.type() === "error" && !/Failed to load resource/.test(m.text()) && consoleErrors.push(m.text()));
page.on("pageerror", (e) => consoleErrors.push(e.message));
const shot = (n, full = true) => page.screenshot({ path: `${SHOTS}/${n}.png`, fullPage: full });

// Synthetic Apple Pencil: a stroke through the given page-relative points (0..1)
async function penStroke(points, { pointerType = "pen", pointerId = 7 } = {}) {
  await page.evaluate(({ points, pointerType, pointerId }) => {
    const c = document.querySelector(".hw-live");
    const r = c.getBoundingClientRect();
    const ev = (type, [x, y], pressure) => c.dispatchEvent(new PointerEvent(type, { bubbles: true, cancelable: true, pointerId, pointerType, isPrimary: true,
      clientX: r.left + x * r.width, clientY: r.top + y * r.height, pressure, buttons: type === "pointerup" ? 0 : 1 }));
    ev("pointerdown", points[0], 0.4);
    for (let i = 1; i < points.length; i++) ev("pointermove", points[i], 0.35 + 0.4 * Math.sin((i / points.length) * Math.PI));
    ev("pointerup", points[points.length - 1], 0);
  }, { points, pointerType, pointerId });
}
const word = (x0, y0) => {
  const pts = [];
  for (let i = 0; i <= 40; i++) pts.push([x0 + i * 0.006, y0 + Math.sin(i / 3) * 0.008]);
  return pts;
};

await test("iPad: Start consultation opens the writing pad", async () => {
  await page.goto(BASE + "/#/");
  await page.getByLabel("Username").fill("dr.kavya");
  await page.getByLabel("Password").fill(PASS);
  await page.getByRole("button", { name: "Sign in" }).click();
  await page.locator(".sidebar").waitFor();
  await page.goto(BASE + "/#/queue");
  await page.locator("tr", { hasText: PATIENT }).first().getByRole("button", { name: "Start consultation" }).click();
  await page.locator(".hw-live").waitFor();
  assert.equal(await page.getByRole("tab", { name: "Handwritten notes" }).getAttribute("aria-selected"), "true");
});

await test("Pencil writing, palm rejection, eraser, undo and autosave", async () => {
  const blank = await page.evaluate(() => document.querySelector(".hw-canvas").toDataURL());
  await penStroke(word(0.08, 0.06));
  await penStroke(word(0.36, 0.06));
  await penStroke(word(0.08, 0.10));
  await page.getByText("Apple Pencil detected").waitFor();
  const inked = await page.evaluate(() => document.querySelector(".hw-canvas").toDataURL());
  assert.notEqual(blank, inked, "ink is drawn on the page");
  // palm/finger touch after the Pencil was used must not draw
  await penStroke(word(0.5, 0.5), { pointerType: "touch", pointerId: 9 });
  assert.equal(await page.evaluate(() => document.querySelector(".hw-canvas").toDataURL()), inked, "finger did not draw");
  // eraser removes the last line, undo brings it back
  await page.getByRole("button", { name: "Eraser", exact: true }).click();
  await penStroke([[0.08, 0.10], [0.2, 0.105]]);
  assert.notEqual(await page.evaluate(() => document.querySelector(".hw-canvas").toDataURL()), inked, "erased");
  await page.getByRole("button", { name: "Undo" }).click();
  assert.equal(await page.evaluate(() => document.querySelector(".hw-canvas").toDataURL()), inked, "undo restores the stroke");
  await page.getByRole("button", { name: "Pen", exact: true }).click();
  await page.locator(".hw-save.saved").waitFor({ timeout: 10000 });
  await shot("40-handwriting-pad", false);
});

await test("second page and clear page", async () => {
  await page.getByRole("button", { name: "+ Page" }).click();
  await page.getByText("Page 2 of 2").waitFor();
  await penStroke(word(0.1, 0.1));
  await page.getByRole("button", { name: "Clear page" }).click();
  await page.getByRole("dialog").getByRole("button", { name: "Clear page" }).click();
  await page.getByRole("button", { name: "Previous page" }).click();
  await page.getByText("Page 1 of 2").waitFor();
});

let consultationId;
await test("Analyse handwriting: side-by-side review, corrections, proposed fields", async () => {
  consultationId = page.url().split("/consultations/")[1];
  await page.getByRole("button", { name: "Analyse handwriting" }).click();
  await page.getByText("Review handwritten notes").waitFor();
  await page.locator(".hw-images img").first().waitFor();
  // Recognition is not configured in this environment: the doctor enters the text (as with iPad Scribble)
  await page.getByLabel("Note text").fill("C/o itchy raised wheals on arms and back x 5/7\nNo fever. No lip swelling\nO/E erythematus wheals, generalised\nImp: ?urticaria\nRx fexofenadine 120mg OD x 2 weeks");
  await page.getByText("Possible misread words").waitFor();
  await page.getByRole("button", { name: /erythematus → erythematous/ }).click();
  assert.match(await page.getByLabel("Note text").inputValue(), /erythematous wheals/);
  await page.locator(".hw-check", { hasText: "erythematus" }).waitFor({ state: "detached" });  // proposals refreshed
  await page.getByText("Proposed consultation fields").waitFor();
  await page.locator(".hw-check", { hasText: "Wheal" }).first().waitFor();
  assert.equal(await page.locator(".hw-check", { hasText: "L50.9" }).locator("input").isChecked(), false, "diagnosis needs an explicit tick");
  await page.locator(".hw-check", { hasText: "L50.9" }).locator("input").check();
  await page.locator(".hw-med .hw-check input").first().check();
  await page.getByText("Recorded as absent").waitFor();
  await shot("41-handwriting-review", false);
});

await test("Accept applies the approved fields to the consultation", async () => {
  await page.getByRole("button", { name: /Accept and add to consultation/ }).click();
  await page.getByText("The approved information was added to the consultation").waitFor();
  await page.getByText("Doctor-approved transcription").waitFor();
  await page.getByRole("tab", { name: "Notes, vitals & diagnosis" }).click();
  assert.match(await page.getByLabel("Chief complaint").inputValue(), /itchy raised wheals/);
  await page.getByText("L50.9").first().waitFor();
  await page.getByRole("tab", { name: "Dermatology assessment" }).click();
  await page.getByRole("group", { name: "Lesion type" }).getByRole("button", { name: "Wheal (hive)", exact: true }).and(page.locator('[aria-pressed="true"]')).waitFor();
  await page.getByRole("tab", { name: /Prescription & orders/ }).click();
  await page.locator("h3", { hasText: "Medicines" }).waitFor();
  const values = await page.evaluate(() => Array.from(document.querySelectorAll("input")).map((i) => i.value));
  assert.ok(values.some((v) => v.includes("From handwritten notes")), "medicine added to the draft with a check-dose note");
  assert.ok(values.includes("Once daily") && values.includes("14"), "frequency and duration taken from the note");
  await shot("42-handwriting-applied-rx", false);
});

await test("decision support runs on the approved information", async () => {
  await page.getByRole("tab", { name: "Handwritten notes" }).click();
  await page.getByRole("button", { name: /Open decision support/ }).click();
  await page.getByRole("region", { name: "Clinical knowledge" }).getByText(/Urticaria|No approved clinical content/).first().waitFor();
  await shot("43-handwriting-to-decision-support", false);
});

await test("finalized: handwriting is read-only and kept with the record", async () => {
  await page.getByRole("button", { name: "Finalize", exact: true }).click();
  await page.getByRole("button", { name: "Finalize and lock" }).click();
  await page.getByText(/This record is locked/).waitFor();
  await page.getByRole("tab", { name: "Handwritten notes" }).click();
  await page.getByText("Original handwriting").waitFor();
  await page.locator(".hw-images img").first().waitFor();
  assert.equal(await page.locator(".hw-live").count(), 0, "no writing surface on a finalized record");
  await page.getByText("Doctor-approved transcription").waitFor();
  await shot("44-handwriting-record", false);
});

await test("portrait iPad layout", async () => {
  await page.setViewportSize({ width: 820, height: 1180 });
  await page.waitForTimeout(300);
  await shot("45-handwriting-record-portrait", false);
});

await test("no uncaught browser errors", async () => { assert.deepEqual(consoleErrors, [], consoleErrors.join(" | ")); });

await browser.close();
const failed = results.filter((r) => !r[1]).length;
console.log(`\n${results.length - failed} passed, ${failed} failed. Screenshots in ${SHOTS}/`);
process.exit(failed ? 1 : 0);
