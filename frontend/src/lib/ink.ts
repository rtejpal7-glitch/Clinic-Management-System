// Vector ink model for handwritten notes. Coordinates are in page units
// (PAGE_W wide, A4 proportions) so ink is independent of screen size and pixel
// density. Pure functions here are unit-tested (tests/ink.test.ts).

export const PAGE_W = 1000;
export const PAGE_H = 1414; // A4 portrait ratio (1 : 1.414)

export type InkPoint = [number, number, number, number]; // x, y, pressure 0..1, ms since stroke start
export interface Stroke { t: "pen"; c: string; s: number; p: InkPoint[] }
export interface Ink { v: 1; strokes: Stroke[] }

export const emptyInk = (): Ink => ({ v: 1, strokes: [] });

export const PEN_COLOURS = [
  { id: "#1d2a44", label: "Ink blue-black" },
  { id: "#1f4fd1", label: "Blue" },
  { id: "#c62828", label: "Red" },
];
export const PEN_SIZES = [
  { id: 1.6, label: "Fine" },
  { id: 2.4, label: "Medium" },
  { id: 3.6, label: "Bold" },
];

/** Stroke width at a point. Pencil pressure varies the width; mouse/touch input has no real pressure. */
export function widthAt(base: number, pressure: number): number {
  const p = pressure > 0 && pressure <= 1 ? pressure : 0.5;
  return base * (0.45 + 0.9 * p);
}

/** Normalise raw pointer pressure: mouse buttons report 0.5, some touches report 0. */
export function normalisePressure(pressure: number, pointerType: string): number {
  if (pointerType !== "pen") return 0.5;
  return pressure > 0 ? Math.min(1, pressure) : 0.5;
}

/** Drop points that are closer than `minDist` page units and round coordinates (smaller saves, same look). */
export function simplify(points: InkPoint[], minDist = 0.8): InkPoint[] {
  if (points.length <= 2) return points.map(roundPoint);
  const out: InkPoint[] = [roundPoint(points[0])];
  for (let i = 1; i < points.length - 1; i++) {
    const a = out[out.length - 1], b = points[i];
    if (Math.hypot(b[0] - a[0], b[1] - a[1]) >= minDist) out.push(roundPoint(b));
  }
  out.push(roundPoint(points[points.length - 1]));
  return out;
}
const roundPoint = (p: InkPoint): InkPoint => [Math.round(p[0] * 10) / 10, Math.round(p[1] * 10) / 10, Math.round(p[2] * 100) / 100, Math.round(p[3])];

function distToSegment(px: number, py: number, ax: number, ay: number, bx: number, by: number) {
  const dx = bx - ax, dy = by - ay;
  const len2 = dx * dx + dy * dy;
  const t = len2 === 0 ? 0 : Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / len2));
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

/** Does an eraser circle at (x, y) touch the stroke? */
export function strokeHit(s: Stroke, x: number, y: number, radius: number): boolean {
  const pts = s.p;
  if (pts.length === 1) return Math.hypot(pts[0][0] - x, pts[0][1] - y) <= radius + s.s;
  for (let i = 1; i < pts.length; i++) {
    if (distToSegment(x, y, pts[i - 1][0], pts[i - 1][1], pts[i][0], pts[i][1]) <= radius + widthAt(s.s, pts[i][2]) / 2) return true;
  }
  return false;
}

/** Draw one stroke. `scale` converts page units to canvas pixels. Smoothed with quadratic curves through midpoints. */
export function drawStroke(ctx: CanvasRenderingContext2D, s: Stroke, scale: number) {
  const p = s.p;
  ctx.strokeStyle = s.c;
  ctx.fillStyle = s.c;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  if (p.length === 1) {
    ctx.beginPath();
    ctx.arc(p[0][0] * scale, p[0][1] * scale, (widthAt(s.s, p[0][2]) * scale) / 2, 0, Math.PI * 2);
    ctx.fill();
    return;
  }
  for (let i = 1; i < p.length; i++) drawSegment(ctx, s, i, scale);
}

/** Draw the piece of the stroke that ends at point i (used for low-latency live drawing). */
export function drawSegment(ctx: CanvasRenderingContext2D, s: Stroke, i: number, scale: number) {
  const p = s.p;
  const a = p[i - 1], b = p[i];
  const prev = i >= 2 ? p[i - 2] : a;
  const m1x = (prev[0] + a[0]) / 2, m1y = (prev[1] + a[1]) / 2;
  const m2x = (a[0] + b[0]) / 2, m2y = (a[1] + b[1]) / 2;
  ctx.strokeStyle = s.c;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.lineWidth = widthAt(s.s, (a[2] + b[2]) / 2) * scale;
  ctx.beginPath();
  if (i === 1) {
    ctx.moveTo(a[0] * scale, a[1] * scale);
    ctx.lineTo(m2x * scale, m2y * scale);
  } else {
    ctx.moveTo(m1x * scale, m1y * scale);
    ctx.quadraticCurveTo(a[0] * scale, a[1] * scale, m2x * scale, m2y * scale);
  }
  ctx.stroke();
}

export function drawInk(ctx: CanvasRenderingContext2D, ink: Ink, scale: number) {
  for (const s of ink.strokes) drawStroke(ctx, s, scale);
}

/** Render a page to PNG (white background, no ruling) for the record and for recognition. */
export function renderPng(ink: Ink, pixelWidth = 1400): string {
  const scale = pixelWidth / PAGE_W;
  const canvas = document.createElement("canvas");
  canvas.width = pixelWidth;
  canvas.height = Math.round(PAGE_H * scale);
  const ctx = canvas.getContext("2d")!;
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  drawInk(ctx, ink, scale);
  return canvas.toDataURL("image/png");
}

/** Replace the first case-insensitive whole-word occurrence of `from` with `to`. */
export function replaceWord(text: string, from: string, to: string): string {
  const esc = from.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return text.replace(new RegExp(`(^|[^A-Za-z])${esc}(?=$|[^A-Za-z])`, "i"), (_m, pre) => `${pre}${to}`);
}
