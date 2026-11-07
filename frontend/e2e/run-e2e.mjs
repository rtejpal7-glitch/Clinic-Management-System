// End-to-end UI tests (Playwright, Chromium) against a running stack with the
// demo seed loaded (scripts/seed_demo.py). Takes a screenshot of each screen.
//   E2E_BASE_URL=http://localhost:5173 node e2e/run-e2e.mjs
import { chromium } from "playwright";
import { mkdirSync, rmSync } from "node:fs";
import assert from "node:assert/strict";

const BASE = process.env.E2E_BASE_URL || "http://localhost:5173";
const PASS = process.env.E2E_PASSWORD || "Clinic-Demo-2026";
const SHOTS = process.env.E2E_SCREENSHOTS || "e2e/screenshots";
rmSync(SHOTS, { recursive: true, force: true });
mkdirSync(SHOTS, { recursive: true });

const results = [];
let page; // assigned below
async function test(name, fn) {
  try {
    await fn();
    results.push([name, true]);
    console.log(`  PASS  ${name}`);
  } catch (e) {
    results.push([name, false]);
    console.log(`  FAIL  ${name}\n        ${e.message.split("\n")[0]}`);
    await page.screenshot({ path: `${SHOTS}/FAILED-${name.replace(/[^a-z0-9]+/gi, "-")}.png`, fullPage: true }).catch(() => {});
  }
}
// Unique synthetic patient per run so the suite can be repeated on the same database.
const SUFFIX = Array.from({ length: 5 }, () => "abcdefghijklmnopqrstuvwxyz"[Math.floor(Math.random() * 26)]).join("");
const SURNAME = "Hiremath" + SUFFIX.charAt(0).toUpperCase() + SUFFIX.slice(1);
const PATIENT = `Varsha ${SURNAME}`;
const MOBILE = "98" + String(Math.floor(Math.random() * 1e8)).padStart(8, "0");

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, locale: "en-IN", timezoneId: "Asia/Kolkata" });
page = await ctx.newPage();
const consoleErrors = [];
page.on("console", (m) => m.type() === "error" && !/Failed to load resource/.test(m.text()) && consoleErrors.push(m.text()));
page.on("pageerror", (e) => consoleErrors.push(e.message));

const shot = (n) => page.screenshot({ path: `${SHOTS}/${n}.png`, fullPage: true });
async function login(user) {
  await page.goto(BASE + "/#/");
  if (await page.getByRole("button", { name: "Sign out" }).isVisible().catch(() => false)) await page.getByRole("button", { name: "Sign out" }).click();
  await page.getByLabel("Username").fill(user);
  await page.getByLabel("Password").fill(PASS);
  await page.getByRole("button", { name: "Sign in" }).click();
  await page.getByRole("navigation", { name: "Today" }).waitFor();
}

await test("login page renders and rejects bad credentials", async () => {
  await page.goto(BASE + "/#/");
  await page.getByLabel("Username").fill("reception");
  await page.getByLabel("Password").fill("wrong-password-1");
  await page.getByRole("button", { name: "Sign in" }).click();
  await page.getByText("Invalid username or password").waitFor();
  await shot("01-login");
});

await test("receptionist dashboard shows metrics and token board", async () => {
  await login("reception");
  await page.getByText("Registered patients").waitFor();
  await page.locator(".now-serving").first().waitFor();
  assert.ok((await page.locator(".now-serving").count()) >= 2);
  await shot("02-dashboard-reception");
});

await test("receptionist cannot see clinical or admin navigation", async () => {
  assert.equal(await page.getByRole("link", { name: "Audit log" }).count(), 0);
  assert.equal(await page.getByRole("link", { name: "Users & roles" }).count(), 0);
  await page.goto(BASE + "/#/audit");
  await page.getByText("You don't have access to this page").waitFor();
});

await test("patient directory lists and searches", async () => {
  await page.goto(BASE + "/#/patients");
  await page.locator("table.data tbody tr").first().waitFor();
  await shot("03-patient-directory");
  const name = (await page.locator("table.data tbody tr .cell-main").first().textContent()).trim();
  await page.getByLabel("Search patients").fill(name.split(" ")[0]);
  await page.waitForTimeout(600);
  assert.ok((await page.locator("table.data tbody tr").count()) >= 1);
});

let newPatientName = "";
await test("register patient with validation and duplicate warning", async () => {
  await page.goto(BASE + "/#/patients/new");
  await page.getByRole("button", { name: "Register patient" }).click();
  await page.getByText("Enter the full name").waitFor();
  await shot("04-register-validation");
  newPatientName = PATIENT;
  await page.getByLabel("Full name").fill(newPatientName);
  await page.getByLabel("Gender").selectOption("FEMALE");
  await page.getByLabel("Date of birth", { exact: true }).fill("1991-08-14");
  await page.getByLabel("Mobile number").fill(MOBILE);
  await page.getByLabel("City / town").fill("Bengaluru");
  await page.getByRole("button", { name: "Register patient" }).click();
  // first run registers; a re-run will hit the duplicate dialog instead
  const dup = page.getByRole("dialog", { name: "This patient may already be registered" });
  const profile = page.getByRole("tab", { name: "Overview" });
  await Promise.race([dup.waitFor(), profile.waitFor()]);
  if (await dup.isVisible()) {
    await shot("05-duplicate-warning");
    await page.getByRole("button", { name: "Open record" }).first().click();
  }
  await profile.waitFor();
  await shot("06-patient-profile");
});

await test("duplicate warning appears for a near-identical registration", async () => {
  await page.goto(BASE + "/#/patients/new");
  await page.getByLabel("Full name").fill(`Varsha ${SURNAME.slice(0, -1)}`);
  await page.getByLabel("Gender").selectOption("FEMALE");
  await page.getByLabel("Date of birth", { exact: true }).fill("1991-08-14");
  await page.getByLabel("Mobile number").fill(MOBILE);
  await page.getByRole("button", { name: "Register patient" }).click();
  await page.getByRole("dialog", { name: "This patient may already be registered" }).waitFor();
  await shot("05-duplicate-warning");
  await page.getByRole("button", { name: "Back to form" }).click();
});

await test("book an appointment through the slot picker", async () => {
  await page.goto(BASE + "/#/appointments?book=1");
  const dlg = page.getByRole("dialog", { name: "Book appointment" });
  await dlg.waitFor();
  await dlg.getByLabel("Patient").fill(SURNAME);
  await page.locator(".combo-list li").first().click();
  await dlg.getByLabel("Doctor").selectOption({ index: 1 });
  const tomorrow = new Date(Date.now() + 86400000).toLocaleDateString("en-CA", { timeZone: "Asia/Kolkata" });
  await dlg.getByLabel("Date").fill(tomorrow);
  await dlg.locator(".slot:not([disabled])").first().waitFor();
  await dlg.locator(".slot:not([disabled])").first().click();
  await shot("07-book-appointment");
  await dlg.getByRole("button", { name: "Book appointment" }).click();
  await page.getByText(new RegExp(`^Booked ${PATIENT}`)).waitFor();
  await shot("08-appointments");
});

await test("waiting queue shows tokens and walk-in flow", async () => {
  await page.goto(BASE + "/#/queue");
  await page.getByRole("heading", { name: "In the queue" }).waitFor();
  await page.getByRole("button", { name: "Add walk-in" }).click();
  const dlg = page.getByRole("dialog", { name: "Add walk-in patient" });
  await dlg.getByLabel("Patient").fill(SURNAME);
  await page.locator(".combo-list li").first().click();
  await dlg.getByLabel("Doctor").selectOption({ label: "Dr. Meera Raghunath" });
  await dlg.getByRole("button", { name: "Issue token" }).click();
  await page.getByText(new RegExp(`^Token \\d+ issued to ${PATIENT}`)).waitFor();
  await page.waitForTimeout(400);
  await shot("09-queue");
});

await test("doctor runs a consultation end to end", async () => {
  await login("dr.meera");
  await page.goto(BASE + "/#/queue");
  const row = page.locator("tr", { hasText: PATIENT }).first();
  await row.getByRole("button", { name: "Start consultation" }).click();
  await page.getByLabel("Chief complaint").waitFor();
  await page.getByLabel("Chief complaint").fill("Dry cough and mild fever for 4 days");
  await page.getByLabel("Examination findings").fill("Throat mildly congested. Chest clear.");
  await page.getByLabel("Systolic").fill("124");
  await page.getByLabel("Diastolic").fill("80");
  await page.getByLabel("Pulse").fill("90");
  await page.getByLabel("Temperature").fill("38.1");
  await page.getByLabel("SpO₂").fill("98");
  await page.getByLabel("Height").fill("160");
  await page.getByLabel("Weight").fill("58");
  await page.getByLabel("Add coded diagnosis (ICD-10)").fill("upper respiratory");
  await page.locator(".combo-list li").first().click();
  await shot("10-consultation-notes");
  await page.getByRole("tab", { name: /Prescription & orders/ }).click();
  await page.getByLabel("Add medicine").fill("paracetamol");
  await page.locator(".combo-list li").first().click();
  await page.getByLabel("Add medicine").fill("cetirizine");
  await page.locator(".combo-list li").first().click();
  await page.getByLabel("Order from catalogue").fill("blood count");
  await page.locator(".combo-list li").first().click();
  await page.getByLabel("Advice (non-medication instructions)").fill("Warm fluids, steam inhalation. Return if breathlessness.");
  await shot("11-prescription");
  await page.getByRole("button", { name: "Save draft" }).click();
  await page.getByText("Draft saved").waitFor();
  await page.getByRole("button", { name: "Finalize", exact: true }).click();
  await page.getByRole("button", { name: "Finalize and lock" }).click();
  await page.getByText(/This record is locked/).waitFor();
  await shot("12-consultation-finalized");
});

await test("finalized consultation: addendum, print and FHIR", async () => {
  await page.getByRole("button", { name: "Add addendum" }).click();
  const dlg = page.getByRole("dialog", { name: "Add addendum" });
  await dlg.getByRole("textbox", { name: "Reason", exact: true }).fill("Patient reported penicillin allergy after visit");
  await dlg.getByRole("textbox", { name: "Addendum", exact: true }).fill("Avoid penicillin group antibiotics. Allergy list updated.");
  await dlg.getByRole("button", { name: "Add addendum" }).click();
  await page.getByText("Avoid penicillin group antibiotics").waitFor();
  await page.getByRole("button", { name: "View FHIR bundle" }).click();
  await page.getByText('"resourceType": "Bundle"').waitFor();
  await shot("13-fhir-bundle");
  await page.getByRole("button", { name: "Close" }).click();
  await page.getByRole("button", { name: "Prescription" }).click();
  await page.locator(".print-doc").waitFor();
  await shot("14-prescription-print");
});

await test("doctor schedule week view", async () => {
  await page.goto(BASE + "/#/schedule");
  await page.locator(".week-day").first().waitFor();
  await shot("15-doctor-schedule");
});

await test("receptionist bills the visit and records payment", async () => {
  await login("reception");
  await page.goto(BASE + "/#/appointments");
  const row = page.locator("tr", { hasText: PATIENT }).filter({ hasText: "Completed" }).first();
  await row.getByRole("button", { name: "Bill" }).click();
  await page.getByRole("button", { name: "Create bill" }).waitFor();
  await page.waitForTimeout(500);
  await page.getByLabel("Add service").selectOption({ label: "Nebulization (₹250)" });
  await shot("16-new-bill");
  await page.getByRole("button", { name: "Create bill" }).click();
  await page.getByRole("button", { name: "Record payment" }).waitFor();
  await page.getByRole("button", { name: "Record payment" }).click();
  const dlg = page.getByRole("dialog", { name: "Record payment" });
  await dlg.getByLabel("Amount (₹)").fill("300");
  await dlg.getByLabel("Method").selectOption("UPI");
  await dlg.getByRole("button", { name: "Record payment" }).click();
  await page.getByText("Payment recorded").waitFor();
  await shot("17-invoice");
});

await test("ABHA linking with mock OTP and consent", async () => {
  await page.getByRole("link", { name: "Patient's billing history" }).click();
  await page.getByRole("tab", { name: "ABHA & consent" }).click();
  await page.getByRole("button", { name: "Link ABHA" }).click();
  const dlg = page.getByRole("dialog", { name: "Link ABHA" });
  await dlg.getByLabel("ABHA number", { exact: true }).fill("9900" + String(Math.floor(Math.random() * 1e10)).padStart(10, "0"));
  await dlg.getByRole("checkbox").check();
  await dlg.getByRole("button", { name: "Send OTP to patient" }).click();
  await dlg.getByLabel("OTP", { exact: true }).fill("123456");
  await dlg.getByRole("button", { name: "Verify and link" }).click();
  await page.getByText("ABHA linked (simulated verification)").waitFor();
  await page.getByRole("button", { name: "Request consent" }).click();
  await page.getByRole("dialog", { name: "Request consent" }).getByRole("button", { name: "Send consent request" }).click();
  await page.getByRole("button", { name: "Simulate grant" }).click();
  await page.getByText("Simulated: patient granted consent").waitFor();
  await shot("18-abha-consent");
});

await test("billing list with daily summary (admin)", async () => {
  await login("admin");
  await page.goto(BASE + "/#/billing");
  await page.getByText("Collected (net)").waitFor();
  await shot("19-billing");
});

await test("admin dashboard includes revenue", async () => {
  await page.goto(BASE + "/#/");
  await page.getByText("Collected today").waitFor();
  await shot("20-dashboard-admin");
});

await test("users & roles, permission matrix", async () => {
  await page.goto(BASE + "/#/users");
  await page.locator("table.data tbody tr").first().waitFor();
  await shot("21-users");
  await page.getByRole("tab", { name: "Roles & permissions" }).click();
  await page.getByText("clinical.finalize").waitFor();
  await shot("22-roles");
});

await test("audit log and integrity verification", async () => {
  await page.goto(BASE + "/#/audit");
  await page.locator("table.data tbody tr").first().waitFor();
  await page.getByRole("button", { name: "Verify integrity" }).click();
  await page.getByText(/Integrity check passed/).waitFor();
  await shot("23-audit");
});

await test("clinic settings", async () => {
  await page.goto(BASE + "/#/settings");
  await page.getByLabel("Clinic name").waitFor();
  await shot("24-settings");
});

await test("responsive: mobile layout", async () => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.goto(BASE + "/#/queue");
  await page.getByRole("heading", { name: "In the queue" }).waitFor();
  await shot("25-mobile-queue");
  await page.setViewportSize({ width: 1440, height: 900 });
});

await test("no uncaught browser errors", async () => {
  if (consoleErrors.length) console.log(consoleErrors.join("\n"));
  assert.deepEqual(consoleErrors, []);
});

await browser.close();
const failed = results.filter(([, ok]) => !ok).length;
console.log(`\n${results.length - failed} passed, ${failed} failed. Screenshots in ${SHOTS}/`);
process.exit(failed ? 1 : 0);
