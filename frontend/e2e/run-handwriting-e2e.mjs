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
const rxRow = (list, name) => list.locator(".hw-row", { has: page.locator(".hw-kind", { hasText: /^Rx$/ }) }).filter({ hasText: name });
const NOTE = "C/o itchy raised wheals on arms and back x 5/7\nNo fever. No lip swelling\nO/E erythematus wheals, generalised\nImp: ?urticaria\n" +
  "Rx fexofenadine 120mg OD x 2 weeks\nApply betadine solution on face every evening for 10 days";
await test("one screen: the text sits beside the writing, with what will be added", async () => {
  consultationId = page.url().split("/consultations/")[1];
  await page.getByRole("heading", { name: "Write", exact: true }).waitFor();
  await page.getByRole("heading", { name: "Text", exact: true }).waitFor();
  // Recognition is not configured in this environment: the doctor writes the text with Scribble (filled here)
  await page.getByText("Automatic conversion of the page is not switched on").waitFor();
  await page.getByLabel("Note text").fill(NOTE);
  await page.getByRole("button", { name: /erythematus → erythematous/ }).click();
  assert.match(await page.getByLabel("Note text").inputValue(), /erythematous wheals/);
  const list = page.getByLabel("Will be added to the consultation");
  await rxRow(list, "Fexofenadine").waitFor();
  // both prescription lines are listed; the one not in the catalogue is added as written
  await rxRow(list, "Betadine").getByText("not in catalogue").waitFor();
  assert.ok(await rxRow(list, "Betadine").getByText("Once daily, evening · 10 days").count());
  // exact matches are ticked; the doctor can leave anything out
  assert.equal(await list.locator(".hw-row", { hasText: "L50.9" }).locator("input").isChecked(), true);
  assert.equal(await rxRow(list, "Fexofenadine").locator("input").isChecked(), true);
  await list.getByRole("button", { name: "Wheal (hive)" }).waitFor();
  const back = list.getByRole("button", { name: "Back", exact: true });
  assert.equal(await back.getAttribute("aria-pressed"), "true");
  await back.click();
  assert.equal(await back.getAttribute("aria-pressed"), "false");
  // treatment details are not read as findings: no "Face" area, and the complaint lasted 5 days, not 10
  assert.equal(await list.getByRole("button", { name: "Face", exact: true }).count(), 0);
  await list.getByRole("button", { name: "5 days", exact: true }).waitFor();
  await page.getByText("Written as absent").waitFor();
  await shot("41-handwriting-two-sections", false);
});

await test("Add to consultation fills notes, assessment, diagnosis and the draft prescription", async () => {
  await page.getByRole("button", { name: /^Add to consultation/ }).click();
  await page.getByText(/Added to the consultation at/).waitFor();
  await page.getByRole("tab", { name: "Notes, vitals & diagnosis" }).click();
  assert.match(await page.getByLabel("Chief complaint").inputValue(), /itchy raised wheals/);
  assert.match(await page.getByLabel("Treatment plan").inputValue(), /betadine solution on face/);
  await page.getByText("L50.9").first().waitFor();
  await page.getByRole("tab", { name: "Dermatology assessment" }).click();
  await page.getByRole("group", { name: "Lesion type" }).getByRole("button", { name: "Wheal (hive)", exact: true }).and(page.locator('[aria-pressed="true"]')).waitFor();
  assert.equal(await page.getByRole("group", { name: "Body location" }).getByRole("button", { name: "Back", exact: true }).getAttribute("aria-pressed"), "false", "left out by the doctor");
  await page.getByRole("tab", { name: /Prescription & orders \(2\)/ }).click();
  await page.locator("h3", { hasText: "Medicines" }).waitFor();
  await page.getByText("Betadine", { exact: true }).waitFor();
  const values = await page.evaluate(() => Array.from(document.querySelectorAll("input")).map((i) => i.value));
  assert.ok(values.some((v) => v.startsWith('Handwritten: "fexofenadine 120mg OD')), "medicine added to the draft with the written line");
  assert.ok(values.includes("Once daily") && values.includes("14"), "frequency and duration taken from the note");
  assert.ok(values.includes("Once daily, evening") && values.includes("10"), "the second line too");
  await shot("42-handwriting-applied-rx", false);
});

await test("adding again after more writing adds only what is new", async () => {
  await page.getByRole("tab", { name: "Handwritten notes" }).click();
  await page.getByText(/Added to the consultation at/).waitFor();
  await page.getByLabel("Note text").fill(NOTE.replace("erythematus", "erythematous") + "\nTab cetirizine 10 mg at night x 5 days");
  await rxRow(page.getByLabel("Will be added to the consultation"), "Cetirizine").waitFor();
  await page.getByRole("button", { name: /Add changes to consultation/ }).click();
  await page.getByText(/Added to the consultation at/).waitFor();
  await page.getByRole("tab", { name: /Prescription & orders \(3\)/ }).click();
  await page.getByText("Betadine", { exact: true }).waitFor();
  const plan = await page.evaluate(() => Array.from(document.querySelectorAll("input")).filter((i) => i.value.startsWith("Handwritten:")).length);
  assert.equal(plan, 3, "three medicines, none repeated");
  await page.getByRole("tab", { name: "Notes, vitals & diagnosis" }).click();
  const tp = await page.getByLabel("Treatment plan").inputValue();
  assert.equal(tp.match(/betadine/g)?.length, 1, "plan lines are not repeated");
  assert.match(tp, /cetirizine/);
});

await test("decision support runs on the approved information", async () => {
  await page.getByRole("tab", { name: "Handwritten notes" }).click();
  await page.getByRole("button", { name: "Decision support" }).click();
  await page.getByRole("region", { name: "Clinical knowledge" }).getByText(/Urticaria|No approved clinical content/).first().waitFor();
  await shot("43-handwriting-to-decision-support", false);
});

await test("finalized: handwriting and approved text are read-only and kept with the record", async () => {
  await page.getByRole("button", { name: "Finalize", exact: true }).click();
  await page.getByRole("button", { name: "Finalize and lock" }).click();
  await page.getByText(/This record is locked/).waitFor();
  await page.getByRole("tab", { name: "Handwritten notes" }).click();
  await page.locator(".hw-images img").first().waitFor();
  assert.equal(await page.locator(".hw-live").count(), 0, "no writing surface on a finalized record");
  await page.locator(".hw-text-record", { hasText: "cetirizine" }).waitFor();
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
