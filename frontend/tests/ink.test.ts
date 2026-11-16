// Unit tests for the handwriting ink model (node --test).
import { test } from "node:test";
import assert from "node:assert/strict";
import { normalisePressure, replaceWord, simplify, strokeHit, widthAt, type Stroke } from "../src/lib/ink.ts";

test("pressure: Pencil pressure is used, mouse/touch get a neutral value", () => {
  assert.equal(normalisePressure(0.8, "pen"), 0.8);
  assert.equal(normalisePressure(0, "pen"), 0.5);
  assert.equal(normalisePressure(1, "mouse"), 0.5);
  assert.equal(normalisePressure(0, "touch"), 0.5);
  assert.ok(widthAt(2, 1) > widthAt(2, 0.2));
});

test("simplify keeps the shape, drops near-duplicate points and rounds", () => {
  const pts = [[0, 0, 0.5, 0], [0.1, 0.1, 0.5, 4], [0.2, 0.15, 0.5, 8], [5, 5, 0.6, 16], [10.123, 10.987, 0.55, 24]] as [number, number, number, number][];
  const s = simplify(pts);
  assert.deepEqual(s[0], [0, 0, 0.5, 0]);
  assert.deepEqual(s[s.length - 1], [10.1, 11, 0.55, 24]);
  assert.equal(s.length, 3);
});

test("eraser hit-testing", () => {
  const stroke: Stroke = { t: "pen", c: "#000", s: 2, p: [[0, 0, 0.5, 0], [100, 0, 0.5, 10]] };
  assert.ok(strokeHit(stroke, 50, 5, 5));
  assert.ok(!strokeHit(stroke, 50, 30, 5));
  assert.ok(!strokeHit(stroke, 130, 0, 5));
  const dot: Stroke = { t: "pen", c: "#000", s: 2, p: [[10, 10, 0.5, 0]] };
  assert.ok(strokeHit(dot, 12, 12, 3));
});

test("correction replaces a whole word once, case-insensitively", () => {
  assert.equal(replaceWord("O/E Erythematus wheals, erythematus edge", "erythematus", "erythematous"), "O/E erythematous wheals, erythematus edge");
  assert.equal(replaceWord("itcy, itcyness", "itcy", "itchy"), "itchy, itcyness");
  assert.equal(replaceWord("no match here", "itcy", "itchy"), "no match here");
});
