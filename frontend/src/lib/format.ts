// Formatting helpers. All clinic-facing times are shown in India Standard Time.

export const TZ = "Asia/Kolkata";

export function rupees(paise: number | null | undefined, opts: { decimals?: boolean } = {}): string {
  if (paise === null || paise === undefined) return "—";
  const v = paise / 100;
  return new Intl.NumberFormat("en-IN", {
    style: "currency",
    currency: "INR",
    minimumFractionDigits: opts.decimals === false ? 0 : 2,
    maximumFractionDigits: opts.decimals === false ? 0 : 2,
  }).format(v);
}

/** Parse "1,234.50" / "1234.5" / "₹ 99" into integer paise; null when invalid. */
export function toPaise(input: string): number | null {
  const clean = input.replace(/[₹,\s]/g, "");
  if (!/^\d+(\.\d{1,2})?$/.test(clean)) return null;
  const [r, p = ""] = clean.split(".");
  return Number(r) * 100 + Number((p + "00").slice(0, 2));
}

export function dateTime(iso?: string | null): string {
  if (!iso) return "—";
  return new Intl.DateTimeFormat("en-IN", { timeZone: TZ, day: "2-digit", month: "short", year: "numeric", hour: "numeric", minute: "2-digit" }).format(new Date(iso));
}
export function time(iso?: string | null): string {
  if (!iso) return "—";
  return new Intl.DateTimeFormat("en-IN", { timeZone: TZ, hour: "numeric", minute: "2-digit" }).format(new Date(iso));
}
export function dateOnly(isoOrDate?: string | null): string {
  if (!isoOrDate) return "—";
  const d = /^\d{4}-\d{2}-\d{2}$/.test(isoOrDate) ? new Date(isoOrDate + "T12:00:00+05:30") : new Date(isoOrDate);
  return new Intl.DateTimeFormat("en-IN", { timeZone: TZ, day: "2-digit", month: "short", year: "numeric" }).format(d);
}

/** Today's date (YYYY-MM-DD) in the clinic timezone. */
export function todayIST(offsetDays = 0): string {
  const d = new Date(Date.now() + offsetDays * 86400000);
  const parts = new Intl.DateTimeFormat("en-CA", { timeZone: TZ, year: "numeric", month: "2-digit", day: "2-digit" }).format(d);
  return parts;
}
export function addDays(ymd: string, n: number): string {
  const d = new Date(ymd + "T12:00:00Z");
  d.setUTCDate(d.getUTCDate() + n);
  return d.toISOString().slice(0, 10);
}
export function weekdayName(ymd: string, style: "short" | "long" = "short"): string {
  return new Intl.DateTimeFormat("en-IN", { weekday: style, timeZone: "UTC" }).format(new Date(ymd + "T12:00:00Z"));
}
/** ISO weekday 1 (Mon) .. 7 (Sun) for a YYYY-MM-DD string. */
export function isoWeekday(ymd: string): number {
  const d = new Date(ymd + "T12:00:00Z").getUTCDay();
  return d === 0 ? 7 : d;
}

export function genderShort(g?: string) {
  return g === "MALE" ? "M" : g === "FEMALE" ? "F" : g === "OTHER" ? "O" : "—";
}
export function ageSex(age?: number, g?: string) {
  return `${age ?? "—"} y / ${genderShort(g)}`;
}

export function humanize(code?: string | null): string {
  if (!code) return "";
  const s = code.replace(/_/g, " ").toLowerCase();
  return s.charAt(0).toUpperCase() + s.slice(1);
}

export function formatAbha(digits: string): string {
  const d = digits.replace(/\D/g, "").slice(0, 14);
  return [d.slice(0, 2), d.slice(2, 6), d.slice(6, 10), d.slice(10, 14)].filter(Boolean).join("-");
}
