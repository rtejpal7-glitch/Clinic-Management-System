#!/usr/bin/env python3
"""
Seed SYNTHETIC dermatology demonstration data across three demo clinics, through
the public API (same validation, RBAC, audit and de-identification as real use).

  * Clinics: the primary clinic plus two demo clinics, each with an administrator,
    a receptionist and two dermatologists; all three join the ClinicFlow network
    under an obviously fictitious "DEMO" agreement reference.
  * History: finalized dermatology consultations (structured assessment, ICD-10
    diagnosis, prescription) with per-doctor prescribing habits, and follow-up
    outcomes for some of them. This is what Panels B and C aggregate. Every
    patient, doctor and clinic is fictitious; the prescribing "habits" are
    invented for demonstration and are NOT clinical recommendations.
  * Today: a few patients waiting for dr.kavya so the consultation screen can be
    tried live.

Clinical decision-support content stays UNREVIEWED (so Panel A shows "No approved
clinical content") unless you pass --approve-content-for-demo, which creates a
"reviewer.demo" account and approves the starter content with the note
"DEMO approval for demonstration only - not a clinical review". Never use that
flag on a system used for patient care.

Usage:
  python3 scripts/seed_derm_demo.py [--api http://localhost:8000/api/v1]
        [--admin-user admin] [--admin-pass Clinic-Demo-2026] [--cases-per-doctor 24]
        [--approve-content-for-demo]
All demo accounts use the password Clinic-Demo-2026.
"""
import argparse
import datetime as dt
import random
import sys

from seed_demo import DEMO_PASSWORD, FIRST_F, FIRST_M, LAST, Api, login, mobile  # reuse helpers

random.seed(20261111)

CLINICS = [
    # code, name, city, state, pincode, admin, (receptionist), doctors [(username, full name, display, reg)]
    ("MAIN", None, None, None, None, None, ("reception", "Lakshmi Prasad"),
     [("dr.kavya", "Kavya Srinivas", "Dr. Kavya Srinivas", "KMC-61742"), ("dr.farhan", "Farhan Siddiqui", "Dr. Farhan Siddiqui", "KMC-63310")]),
    ("TWACHA", "Twachaa Skin Clinic (Demo)", "Mysuru", "Karnataka", "570017", "admin.twacha", ("reception.twacha", "Shobha Gowda"),
     [("dr.nisha", "Nisha Hegde", "Dr. Nisha Hegde", "KMC-70114"), ("dr.rohan", "Rohan Pai", "Dr. Rohan Pai", "KMC-71902")]),
    ("NIRMAL", "Nirmal Dermatology Centre (Demo)", "Hubballi", "Karnataka", "580020", "admin.nirmal", ("reception.nirmal", "Asha Kulkarni"),
     [("dr.imran", "Imran Sheikh", "Dr. Imran Sheikh", "KMC-72550"), ("dr.leela", "Leela Joshi", "Dr. Leela Joshi", "KMC-73068")]),
]

# Condition templates: ICD-10, display, assessment, complaint, age range, sex bias
CONDITIONS = {
    "URTICARIA": ("L50.9", "Urticaria, unspecified", "Itchy raised wheals coming and going",
                  {"problemType": "HIVES_SWELLING", "symptoms": ["ITCHING"], "lesionTypes": ["WHEAL"], "bodySites": ["ARMS", "BACK", "ABDOMEN"],
                   "distribution": "GENERALISED"}, (18, 55), None),
    "TINEA": ("B35.4", "Tinea corporis", "Itchy ring-shaped patches spreading",
              {"problemType": "RASH", "symptoms": ["ITCHING"], "lesionTypes": ["PLAQUE", "SCALE"], "bodySites": ["WAIST", "BUTTOCKS", "ABDOMEN"],
               "configuration": "ANNULAR", "colours": ["ERYTHEMATOUS"], "spreading": True}, (18, 60), None),
    "ACNE": ("L70.0", "Acne vulgaris", "Pimples on the face",
             {"problemType": "ACNE_BREAKOUTS", "lesionTypes": ["COMEDONE", "PAPULE", "PUSTULE"], "bodySites": ["FACE"]}, (15, 28), None),
    "ATOPIC": ("L20.9", "Atopic dermatitis, unspecified", "Dry itchy skin in elbow and knee folds",
               {"problemType": "RASH", "symptoms": ["ITCHING"], "lesionTypes": ["PATCH", "EXCORIATION", "LICHENIFICATION"], "bodySites": ["ARMS", "LEGS"],
                "distribution": "FLEXURAL", "recurring": True, "familyHistory": "Mother has asthma"}, (6, 35), None),
    "SCABIES": ("B86", "Scabies", "Severe itching at night, family also itching",
                {"problemType": "RASH", "symptoms": ["ITCHING", "NIGHT_ITCH", "CONTACTS_AFFECTED"], "lesionTypes": ["PAPULE", "BURROW", "EXCORIATION"],
                 "bodySites": ["FINGER_WEBS", "WRISTS", "WAIST"]}, (8, 60), None),
    "PSORIASIS": ("L40.0", "Psoriasis vulgaris", "Thick scaly patches on elbows and knees",
                  {"problemType": "RASH", "lesionTypes": ["PLAQUE", "SCALE"], "bodySites": ["ARMS", "LEGS", "SCALP"], "distribution": "EXTENSOR",
                   "colours": ["ERYTHEMATOUS"], "recurring": True}, (25, 65), None),
    "MELASMA": ("L81.1", "Chloasma", "Brown patches on cheeks",
                {"problemType": "PIGMENTATION", "lesionTypes": ["PATCH"], "colours": ["HYPERPIGMENTED", "BROWN"], "bodySites": ["FACE"],
                 "distribution": "SYMMETRICAL"}, (24, 48), "FEMALE"),
}

# Invented per-doctor habits: condition -> [(generic code, dose, frequency, days, weight)]
COMMON = {
    "URTICARIA": [("FEXOFENADINE", "120 mg", "Once daily", 14, 5), ("BILASTINE", "20 mg", "Once daily", 14, 3), ("LEVOCETIRIZINE", "5 mg", "Once daily at night", 14, 3),
                  ("CETIRIZINE", "10 mg", "Once daily", 10, 2), ("CALAMINE", "Apply", "Three times daily", 7, 1)],
    "TINEA": [("LULICONAZOLE", "Apply thin layer", "Once daily", 28, 5), ("TERBINAFINE", "250 mg", "Once daily", 21, 3), ("ITRACONAZOLE", "100 mg", "Twice daily", 14, 3),
              ("KETOCONAZOLE_TOP", "Apply thin layer", "Twice daily", 28, 2), ("LEVOCETIRIZINE", "5 mg", "Once daily at night", 14, 2)],
    "ACNE": [("ADAPALENE", "Thin layer", "Once daily at night", 84, 5), ("ADAPALENE_BPO", "Thin layer", "Once daily at night", 84, 3),
             ("CLINDAMYCIN_TOP", "Thin layer", "Twice daily", 56, 2), ("DOXYCYCLINE", "100 mg", "Once daily", 56, 2), ("AZELAIC_ACID", "Thin layer", "Twice daily", 84, 1)],
    "ATOPIC": [("EMOLLIENT", "Liberal amount", "Twice daily", 30, 6), ("MOMETASONE_TOP", "Thin layer", "Once daily", 14, 3), ("TACROLIMUS_003", "Thin layer", "Twice daily", 21, 2),
               ("HYDROCORTISONE_TOP", "Thin layer", "Twice daily", 7, 2), ("CETIRIZINE", "10 mg", "Once daily at night", 14, 1)],
    "SCABIES": [("PERMETHRIN", "Whole body", "Once, repeat after 7 days", 7, 6), ("IVERMECTIN", "12 mg", "Single dose, repeat in 7 days", 1, 2),
                ("HYDROXYZINE", "25 mg", "At night", 7, 2), ("CETIRIZINE", "10 mg", "Once daily", 10, 2)],
    "PSORIASIS": [("CLOBETASOL", "Thin layer", "Twice daily", 14, 3), ("CALCIPOTRIOL", "Thin layer", "Twice daily", 28, 4), ("SALICYLIC_ACID", "Thin layer", "Once daily", 28, 2),
                  ("COAL_TAR", "Apply to scalp", "Twice weekly", 28, 2), ("BETAMETHASONE_VAL", "Thin layer", "Twice daily", 14, 2)],
    "MELASMA": [("SUNSCREEN_SPF30", "Apply", "Every morning and reapply", 90, 6), ("HYDROQUINONE", "Thin layer", "Once daily at night", 56, 3),
                ("TRIPLE_COMBINATION", "Thin layer", "Once daily at night", 56, 2), ("AZELAIC_ACID", "Thin layer", "Twice daily", 84, 2)],
}
NAMES = {"FEXOFENADINE": "fexofenadine", "BILASTINE": "bilastine", "LEVOCETIRIZINE": "levocetirizine", "CETIRIZINE": "cetirizine", "CALAMINE": "calamine",
         "LULICONAZOLE": "luliconazole", "TERBINAFINE": "terbinafine", "ITRACONAZOLE": "itraconazole", "KETOCONAZOLE_TOP": "ketoconazole",
         "ADAPALENE": "adapalene", "ADAPALENE_BPO": "adapalene", "CLINDAMYCIN_TOP": "clindamycin", "DOXYCYCLINE": "doxycycline", "AZELAIC_ACID": "azelaic",
         "EMOLLIENT": "emollient", "MOMETASONE_TOP": "mometasone", "TACROLIMUS_003": "tacrolimus", "HYDROCORTISONE_TOP": "hydrocortisone",
         "PERMETHRIN": "permethrin", "IVERMECTIN": "ivermectin", "HYDROXYZINE": "hydroxyzine", "CLOBETASOL": "clobetasol", "CALCIPOTRIOL": "calcipotriol",
         "SALICYLIC_ACID": "salicylic", "COAL_TAR": "coal tar", "BETAMETHASONE_VAL": "betamethasone", "SUNSCREEN_SPF30": "sunscreen",
         "HYDROQUINONE": "hydroquinone", "TRIPLE_COMBINATION": "fluocinolone"}


def doctor_style(seed):
    """Each doctor re-weights the common options, so their patterns differ."""
    r = random.Random(seed)
    return {cond: [(g, dose, freq, days, max(1, w + r.randint(-2, 3))) for g, dose, freq, days, w in opts] for cond, opts in COMMON.items()}


def ensure_clinic_user(admin, base, username, full_name, role):
    users = {u["username"]: u for u in admin.get("/users")}
    if username not in users:
        temp = "Temp-Setup-" + str(random.randint(1000, 9999)) + "x"
        s, r = admin.post("/users", {"username": username, "fullName": full_name, "password": temp, "roles": [role]})
        if s != 201:
            sys.exit(f"Could not create {username}: {r}")
        c = login(base, username, temp)
        c.post("/auth/change-password", {"currentPassword": temp, "newPassword": DEMO_PASSWORD})
    return next(u for u in admin.get("/users") if u["username"] == username)


def ensure_dermatologist(admin, user, display, reg):
    if user.get("doctorId"):
        return user["doctorId"]
    s, d = admin.post("/doctors", {"userId": user["id"], "displayName": display, "qualification": "MBBS, MD (Dermatology, Venereology & Leprosy)",
                                   "specialization": "Dermatology", "registrationNumber": reg, "registrationCouncil": "Karnataka Medical Council",
                                   "consultationFeePaise": 70000})
    if s != 201:
        sys.exit(f"Doctor profile failed: {d}")
    blocks = [{"weekday": w, "startTime": "08:00", "endTime": "21:00", "slotMinutes": 15} for w in range(1, 8)]
    admin.put(f"/doctors/{d['id']}/schedule", {"blocks": blocks})
    return d["id"]


def new_patient(rec, age_range, sex=None):
    female = sex == "FEMALE" or (sex is None and random.random() < 0.55)
    age = random.randint(*age_range)
    body = {"fullName": f"{random.choice(FIRST_F if female else FIRST_M)} {random.choice(LAST)}",
            "dateOfBirth": dt.date(dt.date.today().year - age, random.randint(1, 12), random.randint(1, 28)).isoformat(),
            "gender": "FEMALE" if female else "MALE", "mobile": mobile(), "city": "Bengaluru", "state": "Karnataka", "pincode": "560041"}
    s, p = rec.post("/patients", body)
    if s == 409:
        s, p = rec.post("/patients", {**body, "confirmNotDuplicate": True, "duplicateOverrideReason": "Synthetic demo data: different person"})
    return p if s == 201 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://localhost:8000/api/v1")
    ap.add_argument("--admin-user", default="admin")
    ap.add_argument("--admin-pass", default=DEMO_PASSWORD)
    ap.add_argument("--cases-per-doctor", type=int, default=24)
    ap.add_argument("--approve-content-for-demo", action="store_true")
    a = ap.parse_args()

    platform = login(a.api, a.admin_user, a.admin_pass)
    me = platform.get("/auth/me")
    if "platform.manage" not in me["permissions"]:
        sys.exit("The admin account needs the platform administrator role to create demo clinics.")
    existing = {f["code"]: f for f in platform.get("/facilities")}

    clinics = []
    for code, name, city, state, pin, admin_user, (rec_user, rec_name), doctors in CLINICS:
        if code == "MAIN":
            admin = platform
        else:
            if code not in existing:
                temp = "Temp-Setup-" + str(random.randint(1000, 9999)) + "x"
                s, r = platform.post("/facilities", {"code": code, "name": name, "specialty": "DERMATOLOGY", "city": city, "state": state, "pincode": pin,
                                                     "adminUsername": admin_user, "adminFullName": f"{name.split(' (')[0]} Administrator", "adminPassword": temp})
                if s != 201:
                    sys.exit(f"Could not create clinic {code}: {r}")
                c = login(a.api, admin_user, temp)
                c.post("/auth/change-password", {"currentPassword": temp, "newPassword": DEMO_PASSWORD})
                print(f"  created clinic {name}")
            admin = login(a.api, admin_user, DEMO_PASSWORD)
        admin.put("/facilities/current/network", {"enabled": True, "agreementRef": f"DEMO-DSA-{code} (fictitious, demonstration only)"})
        rec_u = ensure_clinic_user(admin, a.api, rec_user, rec_name, "RECEPTIONIST")
        docs = []
        for i, (u, full, display, reg) in enumerate(doctors):
            du = ensure_clinic_user(admin, a.api, u, full, "DOCTOR")
            did = ensure_dermatologist(admin, du, display, reg)
            client = login(a.api, u, DEMO_PASSWORD)
            client.doctor_id = did
            client.style = doctor_style(f"{code}-{u}")
            client.username = u
            docs.append(client)
        clinics.append((code, admin, login(a.api, rec_user, DEMO_PASSWORD), docs))

    if a.approve_content_for_demo:
        print("Approving starter content FOR DEMONSTRATION ONLY…")
        ensure_clinic_user(platform, a.api, "reviewer.demo", "Demo Reviewer (not a clinical review)", "CLINICAL_REVIEWER")
        rv = login(a.api, "reviewer.demo", DEMO_PASSWORD)
        content = rv.get("/cds/content", status="UNREVIEWED")
        note = "DEMO approval for demonstration only - not a clinical review"
        n = 0
        for kind, items in (("condition", content["conditions"]), ("red-flag", content["redFlags"]), ("medication", content["medications"]),
                            ("interaction", content["interactions"])):
            for it in items:
                s, _ = rv.post(f"/cds/content/{kind}/{it['id']}/review", {"action": "APPROVE", "notes": note})
                n += s == 200
        print(f"  {n} items marked approved (demo)")

    med_cache = {}

    def med(doc, generic):
        if generic not in med_cache:
            for m in doc.get("/catalogue/medications", q=NAMES[generic]):
                if m.get("genericCode") == generic and not m.get("brandName"):
                    med_cache[generic] = m
                    break
        return med_cache.get(generic)

    print("Dermatology history (synthetic)…")
    total = outcomes = 0
    for code, admin, rec, docs in clinics:
        pool = {}
        for doc in docs:
            for k in range(a.cases_per_doctor):
                cond = random.choice(list(CONDITIONS))
                icd, display, complaint, assess, ages, sex = CONDITIONS[cond]
                key = (cond, k % 6)
                p = pool.get(key) or new_patient(rec, ages, sex)
                if not p:
                    continue
                pool[key] = p
                s, appt = rec.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": doc.doctor_id, "reason": complaint})
                if s != 201:
                    continue
                s, c = doc.post(f"/appointments/{appt['id']}/start-consultation")
                if s != 200:
                    continue
                severity = random.choices(["MILD", "MODERATE", "SEVERE"], [4, 4, 1])[0]
                doc.put(f"/consultations/{c['id']}", {"version": c["version"], "chiefComplaint": complaint,
                                                      "diagnoses": [{"codeSystem": "ICD-10", "code": icd, "display": display}],
                                                      "followUpDate": (dt.date.today() + dt.timedelta(days=random.choice([7, 14, 28]))).isoformat()})
                doc.put(f"/consultations/{c['id']}/derm", {**assess, "severity": severity, "durationValue": random.randint(1, 8),
                                                           "durationUnit": random.choice(["WEEKS", "MONTHS"]),
                                                           "pregnancyStatus": "NOT_PREGNANT" if p["gender"] == "FEMALE" and 15 <= p["ageYears"] <= 48 else "NOT_APPLICABLE"})
                opts = doc.style[cond]
                n_items = 1 if random.random() < 0.45 else 2
                chosen = []
                while len(chosen) < n_items:
                    g = random.choices(opts, [o[4] for o in opts])[0]
                    if g[0] not in [x[0] for x in chosen]:
                        chosen.append(g)
                items = []
                for g, dose, freq, days, _ in chosen:
                    m = med(doc, g)
                    if m:
                        items.append({"medicationId": m["id"], "medicineName": m["genericName"], "strength": m["strength"], "dosageForm": m["dosageForm"],
                                      "dose": dose, "route": m["defaultRoute"], "frequency": freq, "durationDays": days})
                doc.put(f"/consultations/{c['id']}/prescription", {"items": items, "advice": "Synthetic demo record."})
                s, fin = doc.post(f"/consultations/{c['id']}/finalize", {})
                if s == 409:  # demo patient triggered a safety acknowledgement
                    s, fin = doc.post(f"/consultations/{c['id']}/finalize", {"safetyOverrideReason": "Synthetic demo record - reviewed"})
                if s != 200:
                    continue
                total += 1
                if random.random() < 0.6:
                    o = random.choices(["RESOLVED", "IMPROVED", "UNCHANGED", "WORSENED"], [3, 5, 2, 1])[0]
                    doc.post(f"/consultations/{c['id']}/outcome", {"outcome": o, "notes": "Synthetic follow-up outcome"})
                    outcomes += 1
        print(f"  {code}: done")
    print(f"  {total} finalized dermatology consultations, {outcomes} with recorded outcomes")

    # Live demo: patients waiting for dr.kavya today
    code, admin, rec, docs = clinics[0]
    kavya = next(d for d in docs if d.username == "dr.kavya")
    for cond in ("URTICARIA", "TINEA", "ACNE"):
        p = new_patient(rec, CONDITIONS[cond][4], CONDITIONS[cond][5])
        if p:
            rec.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": kavya.doctor_id, "reason": CONDITIONS[cond][2]})
    print("  3 patients waiting for dr.kavya")

    print("\nSign in with (password Clinic-Demo-2026): dr.kavya, dr.farhan (primary clinic); dr.nisha, dr.rohan (Twachaa); dr.imran, dr.leela (Nirmal)")
    print("Clinic admins: admin (platform + primary clinic), admin.twacha, admin.nirmal" + (";  reviewer.demo" if a.approve_content_for_demo else ""))


if __name__ == "__main__":
    main()
