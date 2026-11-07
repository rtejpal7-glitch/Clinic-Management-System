#!/usr/bin/env python3
"""
Seed a ClinicFlow instance with SYNTHETIC demonstration data — through the
public API, so every record passes the same validation, numbering, RBAC and
audit logging as real usage. No real patient data is used. No ABHA numbers
are generated for display as real: the few ABHA links created here use
obviously synthetic values against the MOCK adapter and are flagged as mock.

Usage:
  python3 scripts/seed_demo.py [--api http://localhost:8000/api/v1]
                               [--admin-user admin] [--admin-pass Clinic-Demo-2026]

Creates (idempotently where possible):
  users     dr.meera (Doctor), dr.arjun (Doctor), reception (Receptionist)
  doctors   two profiles with Mon–Sat and Sunday-morning hours
  patients  ~36 synthetic patients with Indian names and addresses
  today     walk-ins and check-ins, several finalized consultations with
            vitals / ICD-10 diagnoses / prescriptions / lab orders, bills and
            payments (cash, UPI, card, partial), one refund
  future    booked appointments and a follow-up over the next week
  abdm      two patients with MOCK ABHA links, granted consent, one mock share
All demo accounts use the password Clinic-Demo-2026.
"""
import argparse
import datetime as dt
import json
import random
import sys
import urllib.error
import urllib.parse
import urllib.request

DEMO_PASSWORD = "Clinic-Demo-2026"
IST = dt.timezone(dt.timedelta(hours=5, minutes=30))
random.seed(20261107)

FIRST_F = ["Ananya", "Priya", "Lakshmi", "Kavya", "Sunita", "Meenakshi", "Fatima", "Deepa", "Shalini", "Rekha", "Aisha", "Pooja", "Nandini", "Geeta", "Sneha", "Radha", "Jyoti", "Farah"]
FIRST_M = ["Rahul", "Arun", "Suresh", "Vikram", "Mohammed", "Karthik", "Rajesh", "Imran", "Sanjay", "Naveen", "Prakash", "Joseph", "Harish", "Manoj", "Abdul", "Ravi", "Gaurav", "Siddharth"]
LAST = ["Sharma", "Iyer", "Reddy", "Nair", "Gowda", "Patel", "Khan", "Rao", "Menon", "Shetty", "Kulkarni", "Fernandes", "Hegde", "Pillai", "Das", "Mukherjee", "Bhat", "Qureshi"]
PLACES = [("Jayanagar", "Bengaluru", "Bengaluru Urban", "Karnataka", "560041"), ("Indiranagar", "Bengaluru", "Bengaluru Urban", "Karnataka", "560038"),
          ("Whitefield", "Bengaluru", "Bengaluru Urban", "Karnataka", "560066"), ("Malleshwaram", "Bengaluru", "Bengaluru Urban", "Karnataka", "560003"),
          ("Koramangala", "Bengaluru", "Bengaluru Urban", "Karnataka", "560034"), ("Hosur Road", "Hosur", "Krishnagiri", "Tamil Nadu", "635109"),
          ("Vijayanagar", "Mysuru", "Mysuru", "Karnataka", "570017")]
LANGS = ["Kannada", "English", "Hindi", "Tamil", "Telugu", "Malayalam", "Urdu"]

VISITS = [  # complaint, ICD-10 code, display, meds [(name query, dose, freq, days)], lab queries, vitals tweak
    ("Fever, sore throat and body ache for 2 days", "J02.9", "Acute pharyngitis, unspecified", [("paracetamol", "1 tablet", "1-1-1", 3), ("cetirizine", "1 tablet", "0-0-1", 5)], ["cbc"], {"temperatureC": 38.6}),
    ("Follow-up of blood pressure; occasional headache", "I10", "Essential (primary) hypertension", [("amlodipine", "1 tablet", "1-0-0", 30)], ["lipid", "creatinine"], {"bpSystolic": 152, "bpDiastolic": 94}),
    ("Increased thirst and frequent urination", "E11.9", "Type 2 diabetes mellitus without complications", [("metformin", "1 tablet", "1-0-1", 30)], ["hba1c", "fasting"], {"weightKg": 82}),
    ("Loose stools since yesterday, mild cramps", "A09", "Other gastroenteritis and colitis of infectious and unspecified origin", [("oral rehydration", "1 sachet in 1 L water", "SOS", 3), ("ondansetron", "1 tablet", "SOS", 2)], [], {"pulse": 96}),
    ("Burning on urination for 3 days", "N39.0", "Urinary tract infection, site not specified", [("nitrofurantoin", "1 capsule", "1-0-1", 5)], ["urine"], {"temperatureC": 37.8}),
    ("Lower back pain after lifting", "M54.5", "Low back pain", [("ibuprofen", "1 tablet", "1-0-1", 5)], [], {}),
    ("Acidity and upper abdominal discomfort after meals", "K21.9", "Gastro-oesophageal reflux disease without oesophagitis", [("pantoprazole", "1 tablet", "1-0-0", 14)], [], {}),
    ("Tiredness, feeling cold", "E03.9", "Hypothyroidism, unspecified", [("levothyroxine", "1 tablet", "1-0-0", 30)], ["tsh"], {"pulse": 62}),
]


class Api:
    def __init__(self, base, token=None):
        self.base, self.token = base.rstrip("/"), token

    def call(self, method, path, body=None, params=None, ok=(200, 201)):
        url = self.base + path + ("?" + urllib.parse.urlencode({k: v for k, v in (params or {}).items() if v is not None}) if params else "")
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": "application/json", **({"Authorization": f"Bearer {self.token}"} if self.token else {})})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.status, json.loads(r.read() or b"null")
        except urllib.error.HTTPError as e:
            payload = json.loads(e.read() or b"null")
            if e.code in ok:
                return e.code, payload
            return e.code, payload

    def get(self, path, **params):
        return self.call("GET", path, params=params)[1]

    def post(self, path, body=None):
        return self.call("POST", path, body or {})

    def put(self, path, body=None):
        return self.call("PUT", path, body or {})


def login(base, user, pw):
    s, r = Api(base).post("/auth/login", {"username": user, "password": pw})
    if s != 200:
        sys.exit(f"Login failed for {user}: {r}")
    return Api(base, r["token"])


def ensure_user(admin, base, username, full_name, role):
    existing = {u["username"]: u for u in admin.get("/users")}
    if username not in existing:
        temp = "Temp-Setup-" + str(random.randint(1000, 9999)) + "x"
        s, r = admin.post("/users", {"username": username, "fullName": full_name, "password": temp, "roles": [role]})
        if s != 201:
            sys.exit(f"Could not create {username}: {r}")
        c = login(base, username, temp)
        c.post("/auth/change-password", {"currentPassword": temp, "newPassword": DEMO_PASSWORD})
        print(f"  created user {username} ({role})")
    else:
        print(f"  user {username} already exists")
    return next(u for u in admin.get("/users") if u["username"] == username)


def ensure_doctor(admin, user, display, qual, spec, reg, fee):
    if user.get("doctorId"):
        return user["doctorId"]
    s, d = admin.post("/doctors", {"userId": user["id"], "displayName": display, "qualification": qual, "specialization": spec,
                                   "registrationNumber": reg, "registrationCouncil": "Karnataka Medical Council", "consultationFeePaise": fee})
    if s != 201:
        sys.exit(f"Doctor profile failed: {d}")
    blocks = [{"weekday": w, "startTime": "09:00", "endTime": "13:30", "slotMinutes": 15} for w in range(1, 7)]
    blocks += [{"weekday": w, "startTime": "16:00", "endTime": "22:00", "slotMinutes": 15} for w in range(1, 7)]
    blocks += [{"weekday": 7, "startTime": "10:00", "endTime": "13:00", "slotMinutes": 15}]
    admin.put(f"/doctors/{d['id']}/schedule", {"blocks": blocks})
    print(f"  created doctor profile {display}")
    return d["id"]


def mobile():
    return random.choice("6789") + "".join(random.choices("0123456789", k=9))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://localhost:8000/api/v1")
    ap.add_argument("--admin-user", default="admin")
    ap.add_argument("--admin-pass", default=DEMO_PASSWORD)
    a = ap.parse_args()

    print("Signing in as administrator…")
    admin = login(a.api, a.admin_user, a.admin_pass)
    print("Clinic settings…")
    admin.put("/settings", {**admin.get("/settings"), "clinicName": "Sanjeevani Family Clinic (Demo)", "address": "14, 3rd Cross, 9th Block, Jayanagar",
                            "city": "Bengaluru", "state": "Karnataka", "pincode": "560069", "phone": "080-4000-0000",
                            "registrationNumber": "KPME-DEMO-0001"})

    print("Users and doctors…")
    meera = ensure_user(admin, a.api, "dr.meera", "Meera Raghunath", "DOCTOR")
    arjun = ensure_user(admin, a.api, "dr.arjun", "Arjun Kulkarni", "DOCTOR")
    ensure_user(admin, a.api, "reception", "Lakshmi Prasad", "RECEPTIONIST")
    d_meera = ensure_doctor(admin, meera, "Dr. Meera Raghunath", "MBBS, MD (General Medicine)", "General Medicine", "KMC-48211", 60000)
    d_arjun = ensure_doctor(admin, arjun, "Dr. Arjun Kulkarni", "MBBS, DNB (Family Medicine)", "Family Medicine", "KMC-55109", 50000)

    rec = login(a.api, "reception", DEMO_PASSWORD)
    docs = {d_meera: login(a.api, "dr.meera", DEMO_PASSWORD), d_arjun: login(a.api, "dr.arjun", DEMO_PASSWORD)}

    existing = rec.get("/patients", size=1)["total"]
    print(f"Patients… ({existing} already registered)")
    patients = []
    if existing < 30:
        for i in range(36):
            female = i % 2 == 0
            first = random.choice(FIRST_F if female else FIRST_M)
            last = random.choice(LAST)
            area, city, district, state, pin = random.choice(PLACES)
            age = random.choice([random.randint(18, 35), random.randint(36, 60), random.randint(61, 82), random.randint(3, 15)])
            dob = dt.date(dt.date.today().year - age, random.randint(1, 12), random.randint(1, 28)).isoformat()
            body = {"fullName": f"{first} {last}", "dateOfBirth": dob, "gender": "FEMALE" if female else "MALE", "mobile": mobile(),
                    "addressLine": f"{random.randint(1, 240)}, {random.randint(1, 18)}th Main, {area}", "city": city, "district": district,
                    "state": state, "pincode": pin, "bloodGroup": random.choice(["A+", "B+", "O+", "AB+", "O-", None]),
                    "preferredLanguage": random.choice(LANGS), "emergencyContactName": f"{random.choice(FIRST_M + FIRST_F)} {last}",
                    "emergencyContactRelation": random.choice(["Spouse", "Son", "Daughter", "Parent", "Sibling"]), "emergencyContactPhone": mobile()}
            s, p = rec.post("/patients", body)
            if s == 409:  # synthetic name collision with an earlier record: register with a recorded reason
                s, p = rec.post("/patients", {**body, "confirmNotDuplicate": True, "duplicateOverrideReason": "Synthetic demo data: different person"})
            if s == 201:
                patients.append(p)
        print(f"  registered {len(patients)} patients")
    else:
        patients = rec.get("/patients", size=100)["items"]

    # Family sharing a mobile number (demonstrates duplicate rules don't over-trigger)
    meds_cache, inv_cache = {}, {}

    def med(q):
        if q not in meds_cache:
            meds_cache[q] = docs[d_meera].get("/catalogue/medications", q=q)[0]
        return meds_cache[q]

    def inv(q):
        if q not in inv_cache:
            res = docs[d_meera].get("/catalogue/investigations", q=q)
            inv_cache[q] = res[0] if res else None
        return inv_cache[q]

    services = {s["code"]: s for s in rec.get("/billing/services")}

    print("Today's visits…")
    adults = [p for p in patients if p["ageYears"] >= 18]
    random.shuffle(adults)
    seen = 0
    for i, p in enumerate(adults[:10]):
        doctor_id = d_meera if i % 2 == 0 else d_arjun
        s, appt = rec.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": doctor_id, "reason": VISITS[i % len(VISITS)][0].split(",")[0]})
        if s != 201:
            continue
        if i >= 7:  # leave a few waiting in the queue
            if i == 8:
                tok = next(t for t in rec.get("/queue") if t["appointmentId"] == appt["id"])
                rec.post(f"/queue/{tok['id']}/call")
            continue
        doc = docs[doctor_id]
        s, c = doc.post(f"/appointments/{appt['id']}/start-consultation")
        if s != 200:
            continue
        complaint, code, display, meds, labs, tweak = VISITS[i % len(VISITS)]
        vitals = {"bpSystolic": random.randint(110, 132), "bpDiastolic": random.randint(70, 84), "pulse": random.randint(68, 88), "temperatureC": 36.8,
                  "respiratoryRate": 16, "spo2": random.randint(96, 99), "heightCm": random.randint(150, 178), "weightKg": random.randint(52, 80)}
        vitals.update(tweak)
        if i == 1:
            doc.post(f"/patients/{p['id']}/allergies", {"substance": "Sulfonamides", "category": "MEDICATION", "severity": "MODERATE", "reaction": "Skin rash"})
        s, c = doc.put(f"/consultations/{c['id']}", {
            "version": c["version"], "chiefComplaint": complaint, "historyPresentIllness": "Onset as described. No red-flag symptoms reported.",
            "examinationNotes": "General condition fair. Systemic examination unremarkable except as noted.", "vitals": vitals,
            "diagnoses": [{"codeSystem": "ICD-10", "code": code, "display": display}], "treatmentPlan": "Medicines as prescribed. Review with reports.",
            "followUpInstructions": "Return earlier if symptoms worsen.", "followUpDate": (dt.date.today() + dt.timedelta(days=7)).isoformat()})
        items = []
        for q, dose, freq, days in meds:
            m = med(q)
            items.append({"medicationId": m["id"], "medicineName": m["genericName"], "strength": m["strength"], "dosageForm": m["dosageForm"],
                          "dose": dose, "route": m["defaultRoute"], "frequency": freq, "durationDays": days, "instructions": "After food" if "tablet" in dose else None})
        orders = [{"investigationId": inv(l)["id"], "priority": "ROUTINE"} for l in labs if inv(l)]
        doc.put(f"/consultations/{c['id']}/prescription", {"items": items, "investigations": orders, "advice": "Adequate fluids and rest."})
        if i < 6:
            doc.post(f"/consultations/{c['id']}/finalize")
            seen += 1
            # Bill and payment
            fee_code = "CONSULT-NEW"
            body = {"patientId": p["id"], "appointmentId": appt["id"], "consultationId": c["id"], "items": [{"serviceId": services[fee_code]["id"]}]}
            if i == 3:
                body["items"].append({"serviceId": services["PROC-INJ"]["id"]})
            if i == 4:
                body.update({"discountPaise": 10000, "discountReason": "Senior citizen concession"})
            s, b = rec.post("/bills", body)
            if s == 201:
                method = ["CASH", "UPI", "CARD", "UPI", "CASH", None][i]
                if method:
                    amt = b["totalPaise"] if i != 2 else b["totalPaise"] // 2
                    rec.post(f"/bills/{b['id']}/payments", {"amountPaise": amt, "method": method, "reference": f"UPI{random.randint(10**9, 10**10)}" if method == "UPI" else None})
                if i == 0:
                    admin.post(f"/bills/{b['id']}/refunds", {"amountPaise": 10000, "method": "CASH", "notes": "Duplicate charge for registration reversed"})
    print(f"  {seen} consultations finalized, others in progress or waiting")

    print("Future appointments…")
    booked = 0
    for p in patients[10:24]:
        doctor_id = random.choice([d_meera, d_arjun])
        for days in range(1, 8):
            date = (dt.datetime.now(IST) + dt.timedelta(days=days)).date().isoformat()
            slots = [s for s in rec.get(f"/doctors/{doctor_id}/slots", date=date) if s["available"]]
            if slots:
                s, _ = rec.post("/appointments", {"patientId": p["id"], "doctorId": doctor_id, "startAt": random.choice(slots[:20])["startAt"],
                                                  "reason": random.choice(["Routine check-up", "Fever", "Diabetes review", "Cough", "Skin rash", "BP review"])})
                if s == 201:
                    booked += 1
                break
    print(f"  {booked} appointments booked over the next week")

    print("ABDM (mock) demonstration…")
    for p in adults[:2]:
        synthetic = "99-0000-" + "".join(random.choices("0123456789", k=4)) + "-" + "".join(random.choices("0123456789", k=4))
        s, t = rec.post(f"/patients/{p['id']}/abha/verify-init", {"idType": "ABHA_NUMBER", "value": synthetic, "authMethod": "MOBILE_OTP", "patientConsented": True})
        if s == 200 and t["status"] == "SUCCESS":
            rec.post(f"/abdm/transactions/{t['id']}/verify-confirm", {"otp": "123456"})
            today = dt.date.today()
            s, cons = rec.post(f"/patients/{p['id']}/consents", {"direction": "HIP_SHARE", "purposeCode": "CAREMGT", "hiTypes": ["OPConsultation", "Prescription"],
                                                                 "dateFrom": (today - dt.timedelta(days=365)).isoformat(), "dateTo": (today + dt.timedelta(days=1)).isoformat(),
                                                                 "expiresAt": (dt.datetime.now(dt.timezone.utc) + dt.timedelta(days=30)).isoformat()})
            if s == 201:
                rec.post(f"/consents/{cons['id']}/mock-respond", {"decision": "GRANT"})
    first = adults[0]
    timeline = docs[d_meera].get(f"/patients/{first['id']}/timeline")
    if timeline and timeline[0]["status"] == "FINALIZED":
        s, r = docs[d_meera].post(f"/consultations/{timeline[0]['consultationId']}/abdm-share")
        print(f"  mock share: {r.get('transaction', {}).get('status', r)}")

    print("\nDone. Sign in at the web app with:")
    print(f"  admin / {a.admin_pass}    dr.meera / {DEMO_PASSWORD}    dr.arjun / {DEMO_PASSWORD}    reception / {DEMO_PASSWORD}")


if __name__ == "__main__":
    main()
