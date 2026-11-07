// Client-side validation mirrors the backend rules so users get immediate
// feedback. The backend remains the authority and re-validates everything.

export type Errors = Record<string, string>;

export const isIndianMobile = (s: string) => /^[6-9]\d{9}$/.test(s);
export const isPincode = (s: string) => /^[1-9]\d{5}$/.test(s);
export const isEmail = (s: string) => /^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(s);

export interface PatientForm {
  fullName: string;
  dateOfBirth: string;
  gender: string;
  mobile: string;
  email?: string;
  pincode?: string;
  emergencyContactPhone?: string;
}

export function validatePatient(p: PatientForm, today = new Date()): Errors {
  const e: Errors = {};
  const name = p.fullName.trim();
  if (name.length < 2 || name.length > 120) e.fullName = "Enter the full name (2–120 characters)";
  else if (/[0-9<>{}]/.test(name)) e.fullName = "Name contains invalid characters";
  if (!/^\d{4}-\d{2}-\d{2}$/.test(p.dateOfBirth)) e.dateOfBirth = "Enter the date of birth";
  else {
    const dob = new Date(p.dateOfBirth + "T00:00:00");
    const min = new Date(today);
    min.setFullYear(min.getFullYear() - 130);
    if (dob > today) e.dateOfBirth = "Date of birth cannot be in the future";
    else if (dob < min) e.dateOfBirth = "Check the year";
  }
  if (!["MALE", "FEMALE", "OTHER", "UNDISCLOSED"].includes(p.gender)) e.gender = "Select a gender";
  if (!isIndianMobile(p.mobile)) e.mobile = "Enter a 10-digit mobile number starting with 6–9";
  if (p.email && !isEmail(p.email)) e.email = "Check the email address";
  if (p.pincode && !isPincode(p.pincode)) e.pincode = "Enter a 6-digit PIN code";
  if (p.emergencyContactPhone) {
    if (!isIndianMobile(p.emergencyContactPhone)) e.emergencyContactPhone = "Enter a 10-digit mobile number";
    else if (p.emergencyContactPhone === p.mobile) e.emergencyContactPhone = "Use a different number from the patient's";
  }
  return e;
}

/** Date of birth from an age in years (estimated DOB = 1 July of that year). */
export function dobFromAge(age: number, today = new Date()): string {
  return `${today.getFullYear() - age}-07-01`;
}

export function validatePassword(pw: string, username: string): string | null {
  if (pw.length < 10) return "Use at least 10 characters";
  if (!/[A-Za-z]/.test(pw) || !/\d/.test(pw)) return "Use both letters and numbers";
  if (username && pw.toLowerCase().includes(username.toLowerCase())) return "Don't include your username";
  return null;
}

export const VITAL_RANGES: Record<string, [number, number, string]> = {
  bpSystolic: [40, 300, "Systolic"],
  bpDiastolic: [20, 200, "Diastolic"],
  pulse: [20, 250, "Pulse"],
  temperatureC: [30, 45, "Temperature"],
  respiratoryRate: [4, 80, "Resp. rate"],
  spo2: [50, 100, "SpO₂"],
  heightCm: [30, 250, "Height"],
  weightKg: [1, 400, "Weight"],
};

export function validateVitals(v: Record<string, number | null | undefined>): Errors {
  const e: Errors = {};
  for (const [k, [lo, hi, label]] of Object.entries(VITAL_RANGES)) {
    const val = v[k];
    if (val !== null && val !== undefined && (val < lo || val > hi)) e[`vitals.${k}`] = `${label} should be ${lo}–${hi}`;
  }
  const s = v.bpSystolic, d = v.bpDiastolic;
  if ((s == null) !== (d == null)) e["vitals.bpDiastolic"] = "Enter both BP values";
  else if (s != null && d != null && s <= d) e["vitals.bpDiastolic"] = "Diastolic must be lower than systolic";
  return e;
}

export function bmi(heightCm?: number | null, weightKg?: number | null): number | null {
  if (!heightCm || !weightKg) return null;
  const m = heightCm / 100;
  return Math.round((weightKg / (m * m)) * 10) / 10;
}

/** Mirrors backend BillingMath: proportional discount allocation, tax after discount (round half up). */
export function previewBill(lines: { quantity: number; unitPricePaise: number; taxRateBp: number }[], discountPaise: number) {
  const subtotal = lines.reduce((s, l) => s + l.quantity * l.unitPricePaise, 0);
  let allocated = 0;
  let tax = 0;
  lines.forEach((l, i) => {
    const sub = l.quantity * l.unitPricePaise;
    const disc = i === lines.length - 1 ? discountPaise - allocated : subtotal === 0 ? 0 : Math.floor((discountPaise * sub) / subtotal);
    allocated += disc;
    tax += Math.floor(((sub - disc) * l.taxRateBp + 5000) / 10000);
  });
  return { subtotal, discount: discountPaise, tax, total: subtotal - discountPaise + tax };
}
