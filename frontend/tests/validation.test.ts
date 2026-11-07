// Unit tests for client-side validation and formatting (node --test).
import { test } from "node:test";
import assert from "node:assert/strict";
import { validatePatient, validatePassword, validateVitals, bmi, previewBill, dobFromAge, isIndianMobile } from "../src/lib/validation.ts";
import { toPaise, rupees, formatAbha, addDays, isoWeekday, humanize } from "../src/lib/format.ts";

const ok = { fullName: "Asha Rao", dateOfBirth: "1990-02-03", gender: "FEMALE", mobile: "9876543210" };

test("valid patient passes", () => assert.deepEqual(validatePatient(ok, new Date("2026-11-07")), {}));
test("patient field errors", () => {
  const e = validatePatient({ fullName: "A1", dateOfBirth: "2030-01-01", gender: "", mobile: "12345", pincode: "012345", email: "x@" }, new Date("2026-11-07"));
  assert.deepEqual(Object.keys(e).sort(), ["dateOfBirth", "email", "fullName", "gender", "mobile", "pincode"]);
});
test("emergency contact must differ", () => {
  assert.ok(validatePatient({ ...ok, emergencyContactPhone: ok.mobile }).emergencyContactPhone);
});
test("indian mobile rule", () => {
  assert.ok(isIndianMobile("6000000000"));
  assert.ok(!isIndianMobile("5000000000"));
  assert.ok(!isIndianMobile("98765"));
});
test("dob from age", () => assert.equal(dobFromAge(40, new Date("2026-03-01")), "1986-07-01"));
test("password policy", () => {
  assert.ok(validatePassword("short1", "bob"));
  assert.ok(validatePassword("lettersonlyhere", "bob"));
  assert.ok(validatePassword("reception123", "reception"));
  assert.equal(validatePassword("Clinic-Flow-2026", "admin"), null);
});
test("vitals ranges and BP pair", () => {
  assert.deepEqual(validateVitals({ bpSystolic: 120, bpDiastolic: 80, spo2: 98 }), {});
  const e = validateVitals({ bpSystolic: 80, bpDiastolic: 90, spo2: 120 });
  assert.ok(e["vitals.spo2"] && e["vitals.bpDiastolic"]);
  assert.ok(validateVitals({ bpSystolic: 120 })["vitals.bpDiastolic"]);
});
test("bmi", () => {
  assert.equal(bmi(170, 68), 23.5);
  assert.equal(bmi(null, 60), null);
});
test("bill preview matches backend rules", () => {
  // same cases as backend unit tests
  const t = previewBill([{ quantity: 1, unitPricePaise: 1000, taxRateBp: 0 }, { quantity: 1, unitPricePaise: 1000, taxRateBp: 0 }, { quantity: 1, unitPricePaise: 1000, taxRateBp: 0 }], 100);
  assert.equal(t.total, 2900);
  assert.equal(previewBill([{ quantity: 1, unitPricePaise: 20000, taxRateBp: 1800 }], 5000).tax, 2700);
  assert.equal(previewBill([{ quantity: 1, unitPricePaise: 333, taxRateBp: 1800 }], 0).tax, 60);
  assert.equal(previewBill([{ quantity: 1, unitPricePaise: 50000, taxRateBp: 0 }, { quantity: 1, unitPricePaise: 20000, taxRateBp: 1800 }], 7000).tax, 3240);
});
test("rupee parsing and formatting", () => {
  assert.equal(toPaise("1,234.5"), 123450);
  assert.equal(toPaise("₹ 99"), 9900);
  assert.equal(toPaise("12.345"), null);
  assert.equal(toPaise("abc"), null);
  assert.equal(rupees(123450), "₹1,234.50");
  assert.equal(rupees(10000000, { decimals: false }), "₹1,00,000");
});
test("abha formatting and dates", () => {
  assert.equal(formatAbha("91123456789012"), "91-1234-5678-9012");
  assert.equal(formatAbha("9112"), "91-12");
  assert.equal(addDays("2026-12-31", 1), "2027-01-01");
  assert.equal(isoWeekday("2026-11-08"), 7);
  assert.equal(humanize("PARTIALLY_PAID"), "Partially paid");
});
