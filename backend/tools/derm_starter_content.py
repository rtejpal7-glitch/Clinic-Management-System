#!/usr/bin/env python3
"""
Generates migrations/V011__derm_starter_content.sql from the data below.

STATUS OF THIS CONTENT: DRAFT. It was written by an AI assistant from general
knowledge of published guidelines, for a proof of concept. Every row is inserted
with review_status = 'UNREVIEWED' and content_source = 'STARTER_DRAFT'.
ClinicFlow does not use unreviewed content as decision support: a qualified
clinician must check each item (and its citation) against the current source and
approve it in Clinical content review. Citations are listed so the reviewer can
verify them; they have NOT been verified against the source documents.

Usage:  python3 backend/tools/derm_starter_content.py > backend/migrations/V011__derm_starter_content.sql
(Only regenerate before the migration has been applied anywhere: applied
migrations are checksum-protected. Later changes belong in a new migration or
in the review UI.)
"""
import json

VERIFY = " [citation to be verified by reviewer]"

REFS = {
    "AAD_ACNE_2024": "Reynolds RV, Yeung H, Cheng CE, et al. Guidelines of care for the management of acne vulgaris. J Am Acad Dermatol. 2024;90(5):1006.e1-1006.e30.",
    "AAD_AD_2023": "Sidbury R, Alikhan A, Bercovitch L, et al. Guidelines of care for the management of atopic dermatitis in adults with topical therapies. J Am Acad Dermatol. 2023;89(1):e1-e20.",
    "AAD_NPF_PSO_2021": "Elmets CA, Korman NJ, Prater EF, et al. Joint AAD-NPF guidelines of care for the management and treatment of psoriasis with topical therapy and alternative medicine modalities for psoriasis severity measures. J Am Acad Dermatol. 2021;84(2):432-470.",
    "EAACI_URT_2022": "Zuberbier T, Abdul Latiff AH, Abuzakouk M, et al. The international EAACI/GA2LEN/EuroGuiDerm/APAAACI guideline for the definition, classification, diagnosis, and management of urticaria. Allergy. 2022;77(3):734-766.",
    "IACS_2020": "Engelman D, Yoshizumi J, Hay RJ, et al. The 2020 International Alliance for the Control of Scabies Consensus Criteria for the Diagnosis of Scabies. Br J Dermatol. 2020;183(5):808-820.",
    "ECTODERM_2018": "Rajagopalan M, Inamadar A, Mittal A, et al. Expert Consensus on The Management of Dermatophytosis in India (ECTODERM India). BMC Dermatol. 2018;18(1):6.",
    "EDF_VIT_2013": "Taieb A, Alomar A, Bohm M, et al. Guidelines for the management of vitiligo: the European Dermatology Forum consensus. Br J Dermatol. 2013;168(1):5-19.",
    "IDSA_SSTI_2014": "Stevens DL, Bisno AL, Chambers HF, et al. Practice guidelines for the diagnosis and management of skin and soft tissue infections: 2014 update by the Infectious Diseases Society of America. Clin Infect Dis. 2014;59(2):e10-e52.",
    "EU_HZ_2017": "Werner RN, Nikkels AF, Marinovic B, et al. European consensus-based (S2k) Guideline on the Management of Herpes Zoster. J Eur Acad Dermatol Venereol. 2017;31(1):9-29.",
    "AFP_SD_2015": "Clark GW, Pope SM, Jaboori KA. Diagnosis and treatment of seborrheic dermatitis. Am Fam Physician. 2015;91(3):185-190.",
    "PV_2015": "Gupta AK, Foley KA. Antifungal treatment for pityriasis versicolor. J Fungi (Basel). 2015;1(1):13-29.",
    "MELASMA_IDOJ_2014": "Sarkar R, Arora P, Garg VK, Sonthalia S, Gokhale N. Melasma update. Indian Dermatol Online J. 2014;5(4):426-435.",
    "CD_PRACTICE_2015": "Fonacier L, Bernstein DI, Pacheco K, et al. Contact dermatitis: a practice parameter-update 2015. J Allergy Clin Immunol Pract. 2015;3(3 Suppl):S1-S39.",
    "UK_SJS_2016": "Creamer D, Walsh SA, Dziewulski P, et al. U.K. guidelines for the management of Stevens-Johnson syndrome/toxic epidermal necrolysis in adults 2016. Br J Dermatol. 2016;174(6):1194-1227.",
    "NICE_NG14": "National Institute for Health and Care Excellence. Melanoma: assessment and management (NG14). 2015, updated 2022.",
    "LABEL": "Approved product prescribing information (CDSCO / manufacturer label)",
}


def ref(*keys):
    return [{"citation": REFS[k] + VERIFY, "key": k} for k in keys]


def ref_text(*keys):
    return "; ".join(REFS[k] for k in keys) + VERIFY


# ---------------------------------------------------------------------------
# Medicines (generic products only; brands are added by each clinic).
# Columns: generic_code, generic_name, strength, form, route, category, schedule,
#          common_dosage, warnings, rules, contraindications_text, interactions_text,
#          high_risk, aware, default (dose, freq, days) — defaults are only shown
#          to doctors after the row is APPROVED.
# ---------------------------------------------------------------------------
P = "PREGNANCY"
B = "BREASTFEEDING"
CI = "CONTRAINDICATED"
CA = "CAUTION"


def rule(tag, level, note):
    return {"tag": tag, "level": level, "note": note}


MEDS = [
    # Acne
    ("ADAPALENE", "Adapalene", "0.1% w/w", "GEL", "TOPICAL", "Topical retinoid", "H",
     "Apply a thin layer to affected areas once daily at night.", "Irritation, dryness; photosensitivity - use sunscreen. Avoid eyes, lips, mucosa.",
     [rule(P, CA, "Topical retinoids are generally avoided in pregnancy")], "Hypersensitivity", "Other irritant topical agents increase irritation",
     False, None, ("Thin layer", "Once daily at night", 84)),
    ("TRETINOIN_TOP", "Tretinoin", "0.025% w/w", "CREAM", "TOPICAL", "Topical retinoid", "H",
     "Apply a thin layer once daily at night.", "Irritation, photosensitivity.",
     [rule(P, CI, "Avoid topical tretinoin in pregnancy")], "Pregnancy; hypersensitivity", "Benzoyl peroxide may inactivate tretinoin if applied together",
     False, None, ("Thin layer", "Once daily at night", 84)),
    ("ADAPALENE_BPO", "Adapalene + Benzoyl Peroxide", "0.1% + 2.5% w/w", "GEL", "TOPICAL", "Topical retinoid combination", "H",
     "Apply a thin layer once daily in the evening.", "Irritation; benzoyl peroxide bleaches fabric and hair.",
     [rule(P, CA, "Topical retinoid component - generally avoided in pregnancy")], "Hypersensitivity", None,
     False, None, ("Thin layer", "Once daily at night", 84)),
    ("BENZOYL_PEROXIDE", "Benzoyl Peroxide", "2.5% w/w", "GEL", "TOPICAL", "Topical antimicrobial (acne)", None,
     "Apply once or twice daily to affected areas.", "Dryness, irritation; bleaches fabric.", [], "Hypersensitivity", None,
     False, None, ("Thin layer", "Once daily", 84)),
    ("CLINDAMYCIN_TOP", "Clindamycin Phosphate", "1% w/w", "GEL", "TOPICAL", "Topical antibiotic (acne)", "H",
     "Apply twice daily; use with benzoyl peroxide to reduce resistance.", "Not as monotherapy for acne (resistance).", [],
     "Hypersensitivity; history of antibiotic-associated colitis", None, False, None, ("Thin layer", "Twice daily", 84)),
    ("NADIFLOXACIN", "Nadifloxacin", "1% w/w", "CREAM", "TOPICAL", "Topical antibiotic (acne)", "H",
     "Apply twice daily.", "Avoid monotherapy for long periods.", [], "Hypersensitivity to quinolones", None, False, None,
     ("Thin layer", "Twice daily", 56)),
    ("AZELAIC_ACID", "Azelaic Acid", "20% w/w", "CREAM", "TOPICAL", "Topical (acne / pigmentation)", None,
     "Apply twice daily.", "Transient burning or itching.", [], "Hypersensitivity", None, False, None,
     ("Thin layer", "Twice daily", 84)),
    ("DOXYCYCLINE", "Doxycycline", "100 mg", "CAPSULE", "ORAL", "Tetracycline antibiotic", "H",
     "Acne: 100 mg once daily, usually limited to about 3 months with topical therapy.",
     "Photosensitivity; take with water, upright, not at bedtime (oesophagitis).",
     [rule(P, CI, "Tetracyclines: tooth discolouration / bone effects in fetus"),
      rule("AGE_UNDER_8", CI, "Tetracyclines stain developing teeth"),
      rule(B, CA, "Prefer alternatives while breastfeeding")],
     "Pregnancy; children under 8 years; hypersensitivity to tetracyclines",
     "Calcium, iron, antacids reduce absorption; isotretinoin (intracranial hypertension)", False, "ACCESS",
     ("100 mg", "Once daily", 84)),
    ("MINOCYCLINE", "Minocycline", "50 mg", "TABLET", "ORAL", "Tetracycline antibiotic", "H",
     "Acne: 50-100 mg daily as per label.", "Dizziness, pigmentation, rare autoimmune reactions.",
     [rule(P, CI, "Tetracycline class"), rule("AGE_UNDER_8", CI, "Tetracycline class")],
     "Pregnancy; children under 8 years", "Isotretinoin (intracranial hypertension); calcium/iron", False, None,
     ("50 mg", "Twice daily", 84)),
    ("ISOTRETINOIN", "Isotretinoin", "20 mg", "CAPSULE", "ORAL", "Systemic retinoid", "X",
     "Specialist use. Weight-based dosing per label with laboratory monitoring.",
     "Teratogenic: pregnancy must be excluded and prevented. Monitor lipids and liver function; mood changes.",
     [rule(P, CI, "Severe teratogen"), rule(B, CI, "Contraindicated while breastfeeding")],
     "Pregnancy; breastfeeding; hepatic impairment; hyperlipidaemia; concurrent tetracyclines",
     "Tetracyclines (intracranial hypertension); vitamin A supplements", True, None, (None, None, None)),
    # Topical corticosteroids (potency per conventional classification)
    ("HYDROCORTISONE_TOP", "Hydrocortisone", "1% w/w", "CREAM", "TOPICAL", "Topical corticosteroid - mild", None,
     "Apply thinly once or twice daily for short courses.", "Use the lowest potency for the shortest time.", [],
     "Untreated skin infection; rosacea; perioral dermatitis", None, False, None, ("Thin layer", "Twice daily", 7)),
    ("CLOBETASONE", "Clobetasone Butyrate", "0.05% w/w", "CREAM", "TOPICAL", "Topical corticosteroid - moderate", "H",
     "Apply thinly once or twice daily.", "Skin atrophy with prolonged use.", [],
     "Untreated infection; rosacea; acne", None, False, None, ("Thin layer", "Twice daily", 14)),
    ("MOMETASONE_TOP", "Mometasone Furoate", "0.1% w/w", "CREAM", "TOPICAL", "Topical corticosteroid - potent", "H",
     "Apply thinly once daily.", "Avoid face/flexures except short courses; atrophy, striae.",
     [rule("AGE_UNDER_2", CA, "Use with caution in infants")], "Untreated infection; rosacea; acne", None, False, None,
     ("Thin layer", "Once daily", 14)),
    ("BETAMETHASONE_VAL", "Betamethasone Valerate", "0.1% w/w", "CREAM", "TOPICAL", "Topical corticosteroid - potent", "H",
     "Apply thinly once or twice daily.", "Atrophy, striae; not for face long-term.", [],
     "Untreated infection; rosacea; acne", None, False, None, ("Thin layer", "Twice daily", 14)),
    ("CLOBETASOL", "Clobetasol Propionate", "0.05% w/w", "CREAM", "TOPICAL", "Topical corticosteroid - very potent", "H",
     "Apply thinly once or twice daily; short courses, limited weekly amount per label.",
     "Very potent: atrophy, HPA-axis suppression; avoid face, flexures, large areas.",
     [rule("AGE_UNDER_12", CI, "Not recommended in children per label")],
     "Untreated infection; rosacea; acne; perioral dermatitis", None, True, None, ("Thin layer", "Twice daily", 14)),
    ("FLUTICASONE_TOP", "Fluticasone Propionate", "0.05% w/w", "CREAM", "TOPICAL", "Topical corticosteroid - potent", "H",
     "Apply thinly once or twice daily.", "Atrophy with prolonged use.", [], "Untreated infection; rosacea", None, False, None,
     ("Thin layer", "Once daily", 14)),
    # Calcineurin inhibitors
    ("TACROLIMUS_003", "Tacrolimus", "0.03% w/w", "OINTMENT", "TOPICAL", "Topical calcineurin inhibitor", "H",
     "Apply thinly twice daily.", "Burning on application; avoid sun exposure.",
     [rule("AGE_UNDER_2", CI, "Not licensed under 2 years")], "Hypersensitivity; infected lesions", None, False, None,
     ("Thin layer", "Twice daily", 21)),
    ("TACROLIMUS_01", "Tacrolimus", "0.1% w/w", "OINTMENT", "TOPICAL", "Topical calcineurin inhibitor", "H",
     "Apply thinly twice daily.", "Burning on application.",
     [rule("AGE_UNDER_16", CI, "0.1% strength is for 16 years and above")], "Hypersensitivity; infected lesions", None,
     False, None, ("Thin layer", "Twice daily", 21)),
    # Antifungals
    ("LULICONAZOLE", "Luliconazole", "1% w/w", "CREAM", "TOPICAL", "Topical azole antifungal", None,
     "Apply once daily to lesion and surrounding skin.", "Continue beyond clinical clearance per guideline.", [],
     "Hypersensitivity", None, False, None, ("Thin layer", "Once daily", 28)),
    ("KETOCONAZOLE_TOP", "Ketoconazole", "2% w/w", "CREAM", "TOPICAL", "Topical azole antifungal", None,
     "Apply once or twice daily.", "Local irritation.", [], "Hypersensitivity", None, False, None,
     ("Thin layer", "Twice daily", 28)),
    ("KETOCONAZOLE_SHAMPOO", "Ketoconazole", "2% w/v", "SHAMPOO", "TOPICAL", "Antifungal shampoo", None,
     "Lather, leave 3-5 minutes, rinse; two to three times weekly.", "Scalp irritation.", [], "Hypersensitivity", None,
     False, None, ("Apply to scalp", "Twice weekly", 28)),
    ("TERBINAFINE_TOP", "Terbinafine", "1% w/w", "CREAM", "TOPICAL", "Topical allylamine antifungal", None,
     "Apply once or twice daily.", "Local irritation.", [], "Hypersensitivity", None, False, None,
     ("Thin layer", "Once daily", 28)),
    ("TERBINAFINE", "Terbinafine", "250 mg", "TABLET", "ORAL", "Oral allylamine antifungal", "H",
     "250 mg once daily; duration per indication.", "Hepatotoxicity (rare), taste disturbance.",
     [rule(P, CA, "Avoid unless clearly needed"), rule(B, CA, "Excreted in milk")],
     "Active or chronic liver disease", "CYP2D6 substrates", False, None, ("250 mg", "Once daily", 28)),
    ("ITRACONAZOLE", "Itraconazole", "100 mg", "CAPSULE", "ORAL", "Oral triazole antifungal", "H",
     "Dose and duration per indication; take with food.", "Heart failure risk; hepatotoxicity; many interactions.",
     [rule(P, CI, "Avoid in pregnancy")], "Heart failure; pregnancy; interacting drugs",
     "Strong CYP3A4 inhibitor: statins (simvastatin, atorvastatin), several others", False, None,
     ("100 mg", "Twice daily", 14)),
    ("FLUCONAZOLE", "Fluconazole", "150 mg", "TABLET", "ORAL", "Oral triazole antifungal", "H",
     "Dose per indication.", "QT prolongation; hepatotoxicity.", [rule(P, CA, "Avoid, especially high or prolonged doses")],
     "Hypersensitivity to azoles", "QT-prolonging drugs; warfarin", False, None, ("150 mg", "Once weekly", 28)),
    ("CLOTRIMAZOLE", "Clotrimazole", "1% w/w", "CREAM", "TOPICAL", "Topical azole antifungal", None,
     "Apply two to three times daily.", "Local irritation.", [], "Hypersensitivity", None, False, None,
     ("Thin layer", "Twice daily", 28)),
    # Antihistamines
    ("CETIRIZINE", "Cetirizine", "10 mg", "TABLET", "ORAL", "Second-generation H1 antihistamine", None,
     "10 mg once daily (adults).", "Mild drowsiness in some patients.", [], "Severe renal impairment (dose adjust)", "Alcohol, CNS depressants",
     False, None, ("10 mg", "Once daily", 14)),
    ("LEVOCETIRIZINE", "Levocetirizine", "5 mg", "TABLET", "ORAL", "Second-generation H1 antihistamine", None,
     "5 mg once daily (adults).", "Mild drowsiness.", [], "End-stage renal disease", "Alcohol, CNS depressants",
     False, None, ("5 mg", "Once daily", 14)),
    ("FEXOFENADINE", "Fexofenadine", "120 mg", "TABLET", "ORAL", "Second-generation H1 antihistamine", None,
     "120-180 mg once daily (adults).", "Avoid fruit juice at dosing time (reduced absorption).",
     [rule("AGE_UNDER_12", CA, "Tablet strength for 12 years and above")], "Hypersensitivity", "Antacids containing aluminium/magnesium",
     False, None, ("120 mg", "Once daily", 14)),
    ("BILASTINE", "Bilastine", "20 mg", "TABLET", "ORAL", "Second-generation H1 antihistamine", None,
     "20 mg once daily on an empty stomach.", "Take 1 hour before or 2 hours after food.",
     [rule("AGE_UNDER_12", CA, "20 mg tablet for 12 years and above")], "Hypersensitivity", None, False, None,
     ("20 mg", "Once daily", 14)),
    ("HYDROXYZINE", "Hydroxyzine", "25 mg", "TABLET", "ORAL", "Sedating H1 antihistamine", "H",
     "Per label; often at night for itch.", "Sedation; QT prolongation.", [rule(P, CI, "Avoid in early pregnancy")],
     "Prolonged QT; porphyria", "QT-prolonging drugs; CNS depressants", False, None, ("25 mg", "At night", 7)),
    # Antiparasitic
    ("PERMETHRIN", "Permethrin", "5% w/w", "CREAM", "TOPICAL", "Scabicide", None,
     "Apply to the whole body from the neck down, wash off after 8-12 hours; repeat after 7 days.",
     "Treat all close contacts at the same time; wash clothing and bedding.", [], "Hypersensitivity", None, False, None,
     ("Whole body", "Once, repeat after 7 days", 7)),
    ("IVERMECTIN", "Ivermectin", "12 mg", "TABLET", "ORAL", "Oral antiparasitic", "H",
     "Weight-based single dose, repeated after 7-14 days (per label/guidance).", "Not for children under 15 kg.",
     [rule(P, CA, "Safety in pregnancy not established"), rule("AGE_UNDER_5", CA, "Weight under 15 kg not recommended")],
     "Hypersensitivity", "Warfarin (INR changes reported)", False, None, (None, "Single dose, repeat in 7 days", 1)),
    # Antivirals
    ("ACYCLOVIR", "Acyclovir", "800 mg", "TABLET", "ORAL", "Antiviral", "H",
     "Herpes zoster: 800 mg five times daily for 7 days (adults).", "Hydration; dose-adjust in renal impairment.", [],
     "Hypersensitivity", "Nephrotoxic drugs", False, None, ("800 mg", "Five times daily", 7)),
    ("VALACYCLOVIR", "Valacyclovir", "1 g", "TABLET", "ORAL", "Antiviral", "H",
     "Herpes zoster: 1 g three times daily for 7 days (adults).", "Dose-adjust in renal impairment.", [],
     "Hypersensitivity", "Nephrotoxic drugs", False, None, ("1 g", "Three times daily", 7)),
    # Topical antibacterials
    ("MUPIROCIN", "Mupirocin", "2% w/w", "OINTMENT", "TOPICAL", "Topical antibiotic", "H",
     "Apply up to three times daily for up to 10 days.", "Limit duration to reduce resistance.", [], "Hypersensitivity", None,
     False, None, ("Thin layer", "Three times daily", 7)),
    ("FUSIDIC_ACID", "Fusidic Acid", "2% w/w", "CREAM", "TOPICAL", "Topical antibiotic", "H",
     "Apply three to four times daily; short courses.", "Resistance with prolonged use.", [], "Hypersensitivity", None,
     False, None, ("Thin layer", "Three times daily", 7)),
    # Emollients, keratolytics, others
    ("EMOLLIENT", "Emollient (Light Liquid Paraffin + White Soft Paraffin)", "As labelled", "CREAM", "TOPICAL", "Emollient", None,
     "Apply liberally and frequently, especially after bathing.", "Paraffin-based products are flammable on fabrics.", [],
     None, None, False, None, ("Liberal amount", "Twice daily", 30)),
    ("UREA_10", "Urea", "10% w/w", "CREAM", "TOPICAL", "Emollient / keratolytic", None,
     "Apply twice daily.", "Stinging on broken skin.", [], None, None, False, None, ("Thin layer", "Twice daily", 30)),
    ("CALAMINE", "Calamine", "8% w/v", "LOTION", "TOPICAL", "Antipruritic soothing lotion", None,
     "Apply as needed.", "Drying.", [], None, None, False, None, ("Apply", "Three times daily", 7)),
    ("CALCIPOTRIOL", "Calcipotriol", "0.005% w/w", "OINTMENT", "TOPICAL", "Vitamin D analogue", "H",
     "Apply once or twice daily; maximum weekly amount per label.", "Irritation; hypercalcaemia with excessive use.", [],
     "Disorders of calcium metabolism", None, False, None, ("Thin layer", "Twice daily", 28)),
    ("COAL_TAR", "Coal Tar", "As labelled", "SHAMPOO", "TOPICAL", "Keratoplastic", None,
     "Use as directed on the label.", "Staining, odour, photosensitivity.", [], None, None, False, None,
     ("Apply to scalp", "Twice weekly", 28)),
    ("SALICYLIC_ACID", "Salicylic Acid", "3% w/w", "OINTMENT", "TOPICAL", "Keratolytic", None,
     "Apply once or twice daily to scaly plaques.", "Avoid large areas (salicylism) and in young children.",
     [rule("AGE_UNDER_2", CA, "Avoid in young children")], None, None, False, None, ("Thin layer", "Once daily", 28)),
    ("METHOTREXATE", "Methotrexate", "2.5 mg", "TABLET", "ORAL", "Systemic immunosuppressant", "H",
     "Specialist use. ONCE-WEEKLY dosing with folic acid and blood monitoring.",
     "High-alert medicine: daily-dosing errors can be fatal. Monitor CBC, liver and renal function.",
     [rule(P, CI, "Teratogenic"), rule(B, CI, "Contraindicated while breastfeeding")],
     "Pregnancy; breastfeeding; significant hepatic/renal impairment; blood dyscrasias",
     "NSAIDs, trimethoprim/co-trimoxazole, PPIs (reduced clearance)", True, None, (None, "Once weekly", None)),
    ("HYDROQUINONE", "Hydroquinone", "2% w/w", "CREAM", "TOPICAL", "Depigmenting agent", "H",
     "Apply at night to pigmented areas; limited duration.", "Irritation; exogenous ochronosis with prolonged use; strict sun protection.",
     [rule(P, CA, "Avoid in pregnancy")], "Hypersensitivity", None, False, None, ("Thin layer", "Once daily at night", 56)),
    ("TRIPLE_COMBINATION", "Fluocinolone + Hydroquinone + Tretinoin", "0.01% + 4% + 0.05% w/w", "CREAM", "TOPICAL",
     "Depigmenting combination", "H",
     "Apply once at night; short courses (commonly up to 8 weeks).", "Contains a corticosteroid and tretinoin: atrophy, irritation.",
     [rule(P, CI, "Contains tretinoin and hydroquinone")], "Pregnancy; hypersensitivity", None, False, None,
     ("Thin layer", "Once daily at night", 56)),
    ("SUNSCREEN_SPF30", "Broad-spectrum Sunscreen SPF 30+", "As labelled", "LOTION", "TOPICAL", "Photoprotection", None,
     "Apply generously before sun exposure; reapply every 2-3 hours outdoors.", None, [], None, None, False, None,
     ("Apply", "Every morning and reapply", 90)),
    ("PREDNISOLONE", "Prednisolone", "10 mg", "TABLET", "ORAL", "Systemic corticosteroid", "H",
     "Dose and duration per indication; short courses for acute flares.", "Hyperglycaemia, mood change, infection risk; taper after longer courses.",
     [], "Systemic fungal infection; live vaccines with immunosuppressive doses", "NSAIDs (GI bleeding), antidiabetics", False, None,
     (None, None, None)),
    # Existing non-derm rows that take part in interaction checks
    ("ATORVASTATIN", "Atorvastatin", "10 mg", "TABLET", "ORAL", "Statin", "H", None, None, [], None, None, False, None, (None, None, None)),
    ("CALCIUM_CARBONATE_VITD3", "Calcium Carbonate + Vitamin D3", "500 mg + 250 IU", "TABLET", "ORAL", "Supplement", None, None, None, [],
     None, None, False, None, (None, None, None)),
    ("FERROUS_SULPHATE_FOLIC", "Ferrous Sulphate + Folic Acid", "100 mg + 0.5 mg", "TABLET", "ORAL", "Supplement", None, None, None, [],
     None, None, False, None, (None, None, None)),
    ("IBUPROFEN", "Ibuprofen", "400 mg", "TABLET", "ORAL", "NSAID", None, None, None, [], None, None, False, None, (None, None, None)),
    ("DICLOFENAC", "Diclofenac Sodium", "50 mg", "TABLET", "ORAL", "NSAID", "H", None, None, [], None, None, False, None, (None, None, None)),
]

INGREDIENTS = {
    "ADAPALENE_BPO": ["ADAPALENE", "BENZOYL_PEROXIDE"],
    "TRIPLE_COMBINATION": ["FLUOCINOLONE", "HYDROQUINONE", "TRETINOIN_TOP"],
    "CALCIUM_CARBONATE_VITD3": ["CALCIUM", "VITAMIN_D3"],
    "FERROUS_SULPHATE_FOLIC": ["IRON", "FOLIC_ACID"],
}

INTERACTIONS = [
    ("DOXYCYCLINE", "ISOTRETINOIN", "CONTRAINDICATED", "Tetracyclines with isotretinoin: risk of benign intracranial hypertension."),
    ("ISOTRETINOIN", "MINOCYCLINE", "CONTRAINDICATED", "Tetracyclines with isotretinoin: risk of benign intracranial hypertension."),
    ("CALCIUM", "DOXYCYCLINE", "MODERATE", "Calcium reduces tetracycline absorption; separate doses by 2-3 hours."),
    ("DOXYCYCLINE", "IRON", "MODERATE", "Iron reduces tetracycline absorption; separate doses by 2-3 hours."),
    ("CALCIUM", "MINOCYCLINE", "MODERATE", "Calcium reduces tetracycline absorption; separate doses."),
    ("IRON", "MINOCYCLINE", "MODERATE", "Iron reduces tetracycline absorption; separate doses."),
    ("IBUPROFEN", "METHOTREXATE", "MAJOR", "NSAIDs reduce methotrexate clearance - toxicity risk."),
    ("DICLOFENAC", "METHOTREXATE", "MAJOR", "NSAIDs reduce methotrexate clearance - toxicity risk."),
    ("ATORVASTATIN", "ITRACONAZOLE", "MAJOR", "Itraconazole (CYP3A4 inhibitor) raises statin levels - myopathy/rhabdomyolysis risk."),
    ("HYDROXYZINE", "FLUCONAZOLE", "MAJOR", "Additive QT prolongation."),
]

# ---------------------------------------------------------------------------
# Conditions: code, name, ICD-10, summary, refs, features [(token, weight)],
# observations, questions, treatments [(text, generics, line, severities)], referral
# ---------------------------------------------------------------------------
CONDITIONS = [
    dict(code="ACNE_VULGARIS", name="Acne vulgaris", icd="L70.0",
         summary="Chronic inflammatory disorder of the pilosebaceous unit with comedones, papules, pustules and, in severe disease, nodules/cysts.",
         refs=["AAD_ACNE_2024"],
         features=[("PROBLEM:ACNE_BREAKOUTS", 3), ("LESION:COMEDONE", 4), ("LESION:PUSTULE", 2), ("LESION:PAPULE", 1),
                   ("LESION:NODULE", 1), ("LESION:CYST", 1), ("SITE:FACE", 2), ("SITE:CHEST", 1), ("SITE:BACK", 1),
                   ("AGE:ADOLESCENT", 1), ("AGE:YOUNG_ADULT", 1)],
         obs=["Comedones (open/closed) distinguish acne from rosacea and folliculitis.",
              "Grade by lesion type and count; nodules, cysts or scarring indicate severe disease."],
         qs=["Are there blackheads/whiteheads (comedones)?",
             "Any flushing or eye symptoms (suggests rosacea instead)?",
             "Cosmetic, steroid or supplement use that could trigger an acneiform eruption?",
             "Irregular periods, hirsutism (consider hyperandrogenism)?",
             "Is the patient pregnant, planning pregnancy or breastfeeding?"],
         tx=[("Topical retinoid (e.g. adapalene) as base therapy, with benzoyl peroxide", ["ADAPALENE", "BENZOYL_PEROXIDE", "ADAPALENE_BPO"], "FIRST", ["MILD", "MODERATE"]),
             ("Topical clindamycin only in combination with benzoyl peroxide (not as monotherapy)", ["CLINDAMYCIN_TOP", "BENZOYL_PEROXIDE"], "FIRST", ["MILD", "MODERATE"]),
             ("Azelaic acid as an alternative topical agent", ["AZELAIC_ACID"], "SECOND", ["MILD", "MODERATE"]),
             ("Oral doxycycline for moderate-severe inflammatory acne, combined with topical therapy and limited in duration", ["DOXYCYCLINE"], "FIRST", ["MODERATE", "SEVERE"]),
             ("Oral isotretinoin for severe, nodular or scarring acne under a pregnancy-prevention programme (specialist)", ["ISOTRETINOIN"], "SECOND", ["SEVERE"])],
         referral="Severe nodulocystic or scarring acne, or candidates for isotretinoin: dermatologist with monitoring."),
    dict(code="TINEA_CORPORIS", name="Tinea corporis", icd="B35.4",
         summary="Dermatophyte infection of glabrous skin: itchy annular scaly plaques with an active edge.",
         refs=["ECTODERM_2018"],
         features=[("CONFIG:ANNULAR", 4), ("LESION:SCALE", 2), ("LESION:PLAQUE", 1), ("SYMPTOM:ITCHING", 2),
                   ("COLOUR:ERYTHEMATOUS", 1), ("SITE:WAIST", 1), ("SITE:BUTTOCKS", 1), ("SITE:ABDOMEN", 1), ("SPREADING", 1)],
         obs=["Annular plaque with peripheral scale and central clearing; KOH microscopy confirms.",
              "Prior topical steroid use can mask features (tinea incognito)."],
         qs=["Has a steroid-containing cream been used?", "Do family members have similar lesions?",
             "Is the groin, feet or nails also involved?", "Has a KOH mount been done?"],
         tx=[("Topical azole or allylamine antifungal for limited disease, continued beyond clinical clearance", ["LULICONAZOLE", "KETOCONAZOLE_TOP", "TERBINAFINE_TOP", "CLOTRIMAZOLE"], "FIRST", ["MILD", "MODERATE"]),
             ("Oral antifungal (terbinafine or itraconazole) for extensive, recurrent or chronic disease", ["TERBINAFINE", "ITRACONAZOLE"], "FIRST", ["MODERATE", "SEVERE"]),
             ("Avoid topical corticosteroid combination creams", [], "ADJUNCT", []),
             ("Non-sedating antihistamine for itch if needed", ["CETIRIZINE", "LEVOCETIRIZINE"], "ADJUNCT", [])],
         referral="Extensive, recalcitrant or treatment-failure dermatophytosis: dermatologist (consider culture/sensitivity)."),
    dict(code="TINEA_CRURIS", name="Tinea cruris", icd="B35.6",
         summary="Dermatophyte infection of the groin with an advancing scaly border.",
         refs=["ECTODERM_2018"],
         features=[("SITE:GROIN", 4), ("CONFIG:ANNULAR", 2), ("LESION:SCALE", 2), ("SYMPTOM:ITCHING", 2), ("SITE:BUTTOCKS", 1)],
         obs=["Scrotum usually spared (involvement suggests candida).", "Check feet (tinea pedis) as reservoir."],
         qs=["Is the scrotum involved (consider candidiasis)?", "Tight clothing, sweating, diabetes?", "Steroid cream use?"],
         tx=[("Topical azole or allylamine antifungal", ["LULICONAZOLE", "KETOCONAZOLE_TOP", "TERBINAFINE_TOP", "CLOTRIMAZOLE"], "FIRST", ["MILD", "MODERATE"]),
             ("Oral antifungal for extensive or recurrent disease", ["TERBINAFINE", "ITRACONAZOLE"], "FIRST", ["MODERATE", "SEVERE"]),
             ("Keep area dry; avoid steroid-containing combinations", [], "ADJUNCT", [])],
         referral=None),
    dict(code="ATOPIC_DERMATITIS", name="Atopic dermatitis (eczema)", icd="L20.9",
         summary="Chronic relapsing pruritic dermatitis, typically flexural in older children and adults, with personal/family atopy.",
         refs=["AAD_AD_2023"],
         features=[("SYMPTOM:ITCHING", 3), ("DIST:FLEXURAL", 3), ("LESION:LICHENIFICATION", 2), ("LESION:EXCORIATION", 2),
                   ("LESION:PATCH", 1), ("RECURRING", 2), ("AGE:CHILD", 1), ("HISTORY:ATOPY", 1)],
         obs=["Itch is essential to the diagnosis.", "Assess for secondary bacterial or herpetic infection."],
         qs=["Personal or family history of asthma, allergic rhinitis or eczema?", "Age of onset?",
             "Triggers (soaps, sweat, wool, stress)?", "Any crusting or punched-out erosions (infection)?"],
         tx=[("Regular emollients for all patients", ["EMOLLIENT", "UREA_10"], "FIRST", []),
             ("Topical corticosteroid matched to site and severity (low potency for face/flexures)", ["HYDROCORTISONE_TOP", "CLOBETASONE", "MOMETASONE_TOP", "FLUTICASONE_TOP"], "FIRST", ["MILD", "MODERATE", "SEVERE"]),
             ("Topical calcineurin inhibitor, especially face and flexures", ["TACROLIMUS_003", "TACROLIMUS_01"], "SECOND", ["MILD", "MODERATE", "SEVERE"])],
         referral="Severe or refractory disease (consider phototherapy/systemic therapy), or suspected eczema herpeticum (urgent)."),
    dict(code="PSORIASIS_VULGARIS", name="Psoriasis vulgaris", icd="L40.0",
         summary="Chronic immune-mediated disease with well-demarcated erythematous plaques and silvery scale, often extensor and scalp.",
         refs=["AAD_NPF_PSO_2021"],
         features=[("LESION:PLAQUE", 3), ("LESION:SCALE", 3), ("DIST:EXTENSOR", 3), ("SITE:SCALP", 1), ("SITE:NAILS", 1),
                   ("DIST:SYMMETRICAL", 1), ("RECURRING", 1), ("COLOUR:ERYTHEMATOUS", 1)],
         obs=["Well-demarcated plaques with silvery scale; nail pitting supports diagnosis.", "Ask about joint pain (psoriatic arthritis)."],
         qs=["Joint pain or morning stiffness?", "Nail changes?", "Recent infection or new drugs (beta-blockers, lithium)?",
             "Body surface area involved?"],
         tx=[("Topical corticosteroid (potency by site), short courses", ["BETAMETHASONE_VAL", "MOMETASONE_TOP", "CLOBETASOL"], "FIRST", ["MILD", "MODERATE"]),
             ("Vitamin D analogue alone or with a topical corticosteroid", ["CALCIPOTRIOL"], "FIRST", ["MILD", "MODERATE"]),
             ("Keratolytic or coal tar preparations as adjuncts (including scalp)", ["SALICYLIC_ACID", "COAL_TAR"], "ADJUNCT", []),
             ("Systemic therapy (e.g. methotrexate) or phototherapy for moderate-severe disease - specialist", ["METHOTREXATE"], "SECOND", ["SEVERE"])],
         referral="Extensive disease, erythroderma, pustular psoriasis or arthritis: dermatologist/rheumatologist."),
    dict(code="SEBORRHOEIC_DERMATITIS", name="Seborrhoeic dermatitis", icd="L21.9",
         summary="Chronic relapsing dermatitis of sebaceous areas with greasy yellowish scale.",
         refs=["AFP_SD_2015"],
         features=[("DIST:SEBORRHOEIC", 4), ("SITE:SCALP", 2), ("SITE:FACE", 1), ("LESION:SCALE", 2), ("COLOUR:YELLOW", 1), ("RECURRING", 1)],
         obs=["Nasolabial folds, eyebrows, scalp and chest are typical.", "Severe or sudden disease: consider HIV or Parkinson disease."],
         qs=["Dandruff or scalp flaking?", "Worse with stress or cold weather?", "Any immunosuppression?"],
         tx=[("Ketoconazole shampoo or cream", ["KETOCONAZOLE_SHAMPOO", "KETOCONAZOLE_TOP"], "FIRST", []),
             ("Short course of low-potency topical corticosteroid for flares", ["HYDROCORTISONE_TOP"], "SECOND", ["MODERATE", "SEVERE"]),
             ("Coal tar shampoo as an alternative for scalp", ["COAL_TAR"], "ADJUNCT", [])],
         referral=None),
    dict(code="URTICARIA", name="Urticaria", icd="L50.9",
         summary="Transient itchy wheals lasting under 24 hours each, with or without angioedema; acute (<6 weeks) or chronic.",
         refs=["EAACI_URT_2022"],
         features=[("LESION:WHEAL", 5), ("PROBLEM:HIVES_SWELLING", 3), ("SYMPTOM:ITCHING", 2), ("DIST:GENERALISED", 1), ("SYMPTOM:SWELLING", 1)],
         obs=["Individual wheals resolve within 24 hours; longer-lasting lesions suggest urticarial vasculitis.",
              "Check for angioedema and features of anaphylaxis."],
         qs=["Do individual wheals fade within 24 hours?", "Lip, tongue or throat swelling, or breathing difficulty?",
             "New drugs (NSAIDs, antibiotics), foods or infections?", "Duration: under or over 6 weeks?"],
         tx=[("Second-generation H1 antihistamine at standard dose", ["FEXOFENADINE", "BILASTINE", "CETIRIZINE", "LEVOCETIRIZINE"], "FIRST", []),
             ("If inadequate, the guideline allows up-dosing the second-generation antihistamine (up to 4-fold) - prescriber judgement", ["FEXOFENADINE", "BILASTINE", "CETIRIZINE", "LEVOCETIRIZINE"], "SECOND", ["MODERATE", "SEVERE"]),
             ("Short oral corticosteroid course only for severe acute exacerbations", ["PREDNISOLONE"], "ADJUNCT", ["SEVERE"])],
         referral="Chronic urticaria not controlled on up-dosed antihistamines: specialist (omalizumab/ciclosporin). Anaphylaxis: emergency."),
    dict(code="SCABIES", name="Scabies", icd="B86",
         summary="Infestation by Sarcoptes scabiei with intense nocturnal itch, burrows and involvement of close contacts.",
         refs=["IACS_2020"],
         features=[("LESION:BURROW", 5), ("SITE:FINGER_WEBS", 3), ("SYMPTOM:NIGHT_ITCH", 3), ("SYMPTOM:CONTACTS_AFFECTED", 3),
                   ("SITE:WRISTS", 1), ("SITE:GENITALS", 1), ("SITE:WAIST", 1), ("LESION:PAPULE", 1), ("LESION:EXCORIATION", 1),
                   ("SYMPTOM:ITCHING", 1)],
         obs=["Burrows in finger webs/wrists; genital papules in males are characteristic.",
              "IACS criteria: confirmed, clinical or suspected scabies."],
         qs=["Is itching worse at night?", "Are household members or close contacts itching?",
             "Crusted lesions or immunosuppression (crusted scabies)?"],
         tx=[("Permethrin 5% cream to the whole body, repeated after 7 days; treat all contacts simultaneously", ["PERMETHRIN"], "FIRST", []),
             ("Oral ivermectin as an alternative (weight-based, repeat dose)", ["IVERMECTIN"], "SECOND", []),
             ("Antihistamine for itch; itch may persist for weeks after cure", ["CETIRIZINE", "HYDROXYZINE"], "ADJUNCT", []),
             ("Wash clothes and bedding in hot water at the time of treatment", [], "ADJUNCT", [])],
         referral="Crusted (Norwegian) scabies or institutional outbreak."),
    dict(code="MELASMA", name="Melasma", icd="L81.1",
         summary="Acquired symmetrical brown facial hyperpigmentation, worsened by sun exposure and hormones.",
         refs=["MELASMA_IDOJ_2014"],
         features=[("PROBLEM:PIGMENTATION", 3), ("COLOUR:HYPERPIGMENTED", 3), ("COLOUR:BROWN", 1), ("SITE:FACE", 3),
                   ("DIST:SYMMETRICAL", 2), ("DIST:SUN_EXPOSED", 1), ("LESION:PATCH", 1), ("NO_SYMPTOM:ITCHING", 1)],
         obs=["Malar, centrofacial or mandibular pattern.", "Exclude post-inflammatory hyperpigmentation and drug-induced pigmentation."],
         qs=["Pregnancy, oral contraceptives or hormonal therapy?", "Sun exposure and current sunscreen use?",
             "Use of steroid-containing or bleaching creams?"],
         tx=[("Strict broad-spectrum photoprotection for all patients", ["SUNSCREEN_SPF30"], "FIRST", []),
             ("Hydroquinone 2-4% at night for limited periods", ["HYDROQUINONE"], "FIRST", ["MILD", "MODERATE"]),
             ("Triple combination cream for short courses", ["TRIPLE_COMBINATION"], "FIRST", ["MODERATE", "SEVERE"]),
             ("Azelaic acid as an alternative", ["AZELAIC_ACID"], "SECOND", [])],
         referral=None),
    dict(code="VITILIGO", name="Vitiligo", icd="L80",
         summary="Acquired depigmentation with well-defined milk-white macules/patches due to loss of melanocytes.",
         refs=["EDF_VIT_2013"],
         features=[("COLOUR:DEPIGMENTED", 5), ("PROBLEM:PIGMENTATION", 2), ("LESION:MACULE", 1), ("LESION:PATCH", 1),
                   ("DIST:ACRAL", 1), ("DIST:SYMMETRICAL", 1), ("NO_SYMPTOM:ITCHING", 1)],
         obs=["Depigmented (not hypopigmented) under Wood's lamp.", "Screen for associated autoimmune disease (thyroid)."],
         qs=["Is the area completely white or just lighter (pityriasis alba / versicolor)?", "Family history of vitiligo or thyroid disease?",
             "Is it spreading (active disease)?"],
         tx=[("Topical corticosteroid (moderate-potent) for limited extra-facial lesions, intermittent courses", ["MOMETASONE_TOP", "FLUTICASONE_TOP"], "FIRST", []),
             ("Topical calcineurin inhibitor for face and neck", ["TACROLIMUS_003", "TACROLIMUS_01"], "FIRST", []),
             ("Phototherapy (NB-UVB) for extensive or progressive disease - specialist", [], "SECOND", ["MODERATE", "SEVERE"])],
         referral="Rapidly progressive or extensive vitiligo: dermatologist for phototherapy/systemic options."),
    dict(code="PITYRIASIS_VERSICOLOR", name="Pityriasis versicolor", icd="B36.0",
         summary="Superficial Malassezia infection with hypo- or hyperpigmented finely scaling macules on the upper trunk.",
         refs=["PV_2015"],
         features=[("COLOUR:HYPOPIGMENTED", 3), ("COLOUR:HYPERPIGMENTED", 1), ("LESION:MACULE", 2), ("LESION:SCALE", 1),
                   ("SITE:CHEST", 2), ("SITE:BACK", 2), ("SITE:NECK", 1), ("CONFIG:CONFLUENT", 1)],
         obs=["Fine scale on scraping; KOH shows hyphae and spores.", "Pigment may take months to normalise after cure."],
         qs=["Worse in hot humid weather or with sweating?", "Previous similar episodes?"],
         tx=[("Ketoconazole shampoo as a body wash, or topical azole", ["KETOCONAZOLE_SHAMPOO", "KETOCONAZOLE_TOP", "CLOTRIMAZOLE"], "FIRST", []),
             ("Oral azole for extensive or recurrent disease", ["FLUCONAZOLE", "ITRACONAZOLE"], "SECOND", ["MODERATE", "SEVERE"])],
         referral=None),
    dict(code="CONTACT_DERMATITIS", name="Contact dermatitis", icd="L25.9",
         summary="Irritant or allergic dermatitis at sites of contact with an external agent.",
         refs=["CD_PRACTICE_2015"],
         features=[("AGGRAVATING:NEW_PRODUCT", 3), ("AGGRAVATING:OCCUPATIONAL", 2), ("SITE:HANDS", 2), ("SYMPTOM:ITCHING", 2),
                   ("SYMPTOM:BURNING", 1), ("LESION:VESICLE", 1), ("CONFIG:LINEAR", 1), ("DIST:LOCALISED", 1)],
         obs=["Shape and site often reflect the contactant (watch strap, footwear, cosmetics).",
              "Patch testing identifies allergens in suspected allergic contact dermatitis."],
         qs=["Any new cosmetics, hair dye, jewellery, footwear or occupational exposure?", "Does it improve away from work or on holidays?"],
         tx=[("Identify and avoid the irritant or allergen", [], "FIRST", []),
             ("Topical corticosteroid matched to site and severity", ["HYDROCORTISONE_TOP", "MOMETASONE_TOP", "BETAMETHASONE_VAL"], "FIRST", []),
             ("Emollients and barrier protection", ["EMOLLIENT"], "ADJUNCT", [])],
         referral="Chronic or occupational dermatitis: patch testing."),
    dict(code="HERPES_ZOSTER", name="Herpes zoster", icd="B02.9",
         summary="Reactivation of varicella-zoster virus: painful grouped vesicles in a unilateral dermatomal distribution.",
         refs=["EU_HZ_2017"],
         features=[("CONFIG:DERMATOMAL", 5), ("LESION:VESICLE", 3), ("SYMPTOM:PAIN", 3), ("SYMPTOM:BURNING", 1),
                   ("CONFIG:GROUPED", 1), ("DIST:ASYMMETRICAL", 1), ("AGE:OLDER_ADULT", 1)],
         obs=["Pain may precede the rash.", "Ophthalmic division involvement needs same-day ophthalmology review."],
         qs=["When did the rash start (antivirals are most useful within 72 hours)?", "Eye, nose-tip or ear involvement?",
             "Immunosuppression?"],
         tx=[("Oral antiviral, ideally started within 72 hours of rash onset", ["VALACYCLOVIR", "ACYCLOVIR"], "FIRST", []),
             ("Analgesia appropriate to pain severity", [], "ADJUNCT", []),
             ("Calamine or soothing care for lesions", ["CALAMINE"], "ADJUNCT", [])],
         referral="Ophthalmic zoster, immunocompromised or disseminated disease: urgent referral."),
    dict(code="IMPETIGO", name="Impetigo", icd="L01.0",
         summary="Superficial bacterial infection (S. aureus / Strep. pyogenes) with honey-coloured crusts, common in children.",
         refs=["IDSA_SSTI_2014"],
         features=[("LESION:CRUST", 3), ("COLOUR:YELLOW", 2), ("LESION:EROSION", 1), ("LESION:BULLA", 1), ("SITE:FACE", 1),
                   ("AGE:CHILD", 1), ("SPREADING", 1)],
         obs=["Honey-coloured crusts around nose and mouth are typical.", "Bullous form is staphylococcal."],
         qs=["Other children or contacts affected?", "Underlying eczema or scabies?", "Fever or spreading redness (deeper infection)?"],
         tx=[("Topical mupirocin or fusidic acid for limited lesions", ["MUPIROCIN", "FUSIDIC_ACID"], "FIRST", ["MILD", "MODERATE"]),
             ("Oral anti-staphylococcal antibiotic for extensive disease, per local antibiogram", [], "FIRST", ["SEVERE"]),
             ("Hygiene; avoid sharing towels; school exclusion until crusts dry/after treatment per local policy", [], "ADJUNCT", [])],
         referral=None),
]

RED_FLAGS = [
    ("SJS_TEN", "Possible Stevens-Johnson syndrome / toxic epidermal necrolysis",
     {"all": ["SITE:MUCOSA"], "any": ["LESION:BULLA", "LESION:EROSION", "SYMPTOM:SKIN_PAIN", "LESION:VESICLE"]},
     "Mucosal involvement with blistering or painful skin: stop suspect drugs and arrange urgent hospital assessment.", "UK_SJS_2016"),
    ("SKIN_PAIN_BLISTERS_DRUG", "Painful skin with blisters after a new medicine",
     {"all": ["SYMPTOM:SKIN_PAIN"], "any": ["LESION:BULLA", "LESION:EROSION"]},
     "Consider severe cutaneous adverse reaction; review recent drugs and assess urgently.", "UK_SJS_2016"),
    ("FEVER_WIDESPREAD_RASH", "Fever with widespread rash",
     {"all": ["SYMPTOM:FEVER", "DIST:GENERALISED"]},
     "Consider drug reaction (e.g. DRESS), systemic infection or erythroderma; assess vital signs and refer urgently if unwell.", None),
    ("ERYTHRODERMA", "Erythroderma (extensive involvement)",
     {"all": ["EXTENT_GE:80"]},
     "Erythroderma is a dermatological emergency (fluid, temperature, infection risks): urgent referral.", None),
    ("CHANGING_PIGMENTED_LESION", "Changing or bleeding pigmented lesion",
     {"all": ["PROBLEM:GROWTH_LESION"], "any": ["SYMPTOM:LESION_CHANGE", "SYMPTOM:BLEEDING"]},
     "Assess with ABCDE criteria; refer for dermoscopy/biopsy under the suspected-cancer pathway.", "NICE_NG14"),
    ("OPHTHALMIC_ZOSTER", "Zoster involving the eye region",
     {"all": ["CONFIG:DERMATOMAL", "SITE:EYE_PERIOCULAR"]},
     "Same-day ophthalmology assessment; start antiviral therapy promptly.", "EU_HZ_2017"),
    ("NONHEALING_ULCER", "Non-healing ulcer over 6 weeks",
     {"all": ["LESION:ULCER", "DURATION_GE_DAYS:42"]},
     "Consider malignancy, vascular disease or infection; biopsy/referral.", None),
    ("CELLULITIS_SYSTEMIC", "Spreading painful swelling with fever",
     {"all": ["SYMPTOM:SWELLING", "SYMPTOM:PAIN", "SYMPTOM:FEVER"]},
     "Possible cellulitis or deeper infection; assess for sepsis and need for systemic antibiotics/admission.", "IDSA_SSTI_2014"),
    ("ANGIOEDEMA_AIRWAY", "Angioedema / possible anaphylaxis",
     {"any": ["SYMPTOM:BREATHING_DIFFICULTY", "SYMPTOM:LIP_TONGUE_SWELLING"]},
     "Breathing difficulty or lip/tongue swelling: treat as possible anaphylaxis - emergency management.", "EAACI_URT_2022"),
]

ICD = [(c["icd"], c["name"]) for c in CONDITIONS]


def q(s):
    if s is None:
        return "NULL"
    return "'" + str(s).replace("'", "''") + "'"


def arr(xs):
    return "ARRAY[" + ",".join(q(x) for x in xs) + "]::text[]" if xs else "'{}'::text[]"


def main():
    out = []
    w = out.append
    w("-- ClinicFlow V011: DRAFT dermatology starter content (generated by backend/tools/derm_starter_content.py).")
    w("--")
    w("-- Every row below is UNREVIEWED and marked content_source = 'STARTER_DRAFT'. It was drafted by an AI")
    w("-- assistant from general knowledge of the cited guidelines and has NOT been verified against the source")
    w("-- documents. ClinicFlow does not show unreviewed content as decision support; a qualified clinician must")
    w("-- review each item in Clinical content review before it is used.")
    w("")
    w("-- ICD-10 codes for the starter conditions")
    w("INSERT INTO diagnosis_codes (code_system, code, display) VALUES")
    w(",\n".join(f"  ('ICD-10', {q(c)}, {q(n)})" for c, n in ICD))
    w("ON CONFLICT DO NOTHING;")
    w("")
    w("-- Medicines: update the matching generic rows that already exist, insert the rest.")
    for m in MEDS:
        (code, gname, strength, form, route, cat, sched, dosage, warn, rules, ci_text, ix_text, high, aware, d) = m
        ingredients = INGREDIENTS.get(code, [code])
        reference = ref_text("LABEL")
        cols = dict(generic_code=q(code), ingredient_codes=arr(ingredients), category=q(cat), specialty=q("DERMATOLOGY"),
                    routes=arr([route]), available_strengths=q(strength), default_dose=q(d[0]), default_frequency=q(d[1]),
                    default_duration_days=(str(d[2]) if d[2] else "NULL"), common_dosage=q(dosage), warnings=q(warn),
                    contraindication_rules=q(json.dumps(rules)) + "::jsonb", contraindications_text=q(ci_text),
                    interactions_text=q(ix_text), high_risk=("true" if high else "false"), aware_category=q(aware),
                    reference=q(reference), review_status="'UNREVIEWED'", content_source="'STARTER_DRAFT'")
        sets = ", ".join(f"{k} = {v}" for k, v in cols.items())
        w(f"UPDATE medication_catalogue SET {sets}")
        w(f"  WHERE lower(generic_name) = lower({q(gname)}) AND strength = {q(strength)} AND dosage_form = {q(form)} AND brand_name IS NULL;")
        names = ", ".join(cols.keys())
        vals = ", ".join(cols.values())
        w(f"INSERT INTO medication_catalogue (generic_name, strength, dosage_form, default_route, drug_schedule, {names})")
        w(f"SELECT {q(gname)}, {q(strength)}, {q(form)}, {q(route)}, {q(sched)}, {vals}")
        w(f"WHERE NOT EXISTS (SELECT 1 FROM medication_catalogue WHERE lower(generic_name) = lower({q(gname)}) "
          f"AND strength = {q(strength)} AND dosage_form = {q(form)} AND brand_name IS NULL);")
    w("")
    w("-- Interactions (pairs stored in alphabetical order)")
    for a, b, sev, desc in INTERACTIONS:
        x, y = sorted([a, b])
        w(f"INSERT INTO medication_interactions (generic_a, generic_b, severity, description, reference) VALUES "
          f"({q(x)}, {q(y)}, {q(sev)}, {q(desc)}, {q(ref_text('LABEL'))});")
    w("")
    w("-- Conditions with weighted findings, observations, distinguishing questions and treatment options")
    for c in CONDITIONS:
        refs = json.dumps(ref(*c["refs"]))
        reftxt = ref_text(*c["refs"])
        w(f"WITH c AS (INSERT INTO cds_conditions (code, name, icd10_code, summary, refs, content_source) VALUES "
          f"({q(c['code'])}, {q(c['name'])}, {q(c['icd'])}, {q(c['summary'])}, {q(refs)}::jsonb, 'STARTER_DRAFT') RETURNING id)")
        rows = []
        for tok, wt in c["features"]:
            rows.append(f"((SELECT id FROM c), {q(tok)}, {wt})")
        w("INSERT INTO cds_condition_features (condition_id, feature, weight) VALUES\n  " + ",\n  ".join(rows) + ";")
        content = []
        i = 0
        for o in c["obs"]:
            content.append(f"({q(c['code'])}, 'OBSERVATION', {q(o)}, '{{}}'::text[], NULL, '{{}}'::text[], {q(reftxt)}, {i})"); i += 1
        for o in c["qs"]:
            content.append(f"({q(c['code'])}, 'QUESTION', {q(o)}, '{{}}'::text[], NULL, '{{}}'::text[], {q(reftxt)}, {i})"); i += 1
        for text, gens, line, sevs in c["tx"]:
            content.append(f"({q(c['code'])}, 'TREATMENT', {q(text)}, {arr(gens)}, {q(line)}, {arr(sevs)}, {q(reftxt)}, {i})"); i += 1
        if c.get("referral"):
            content.append(f"({q(c['code'])}, 'REFERRAL', {q(c['referral'])}, '{{}}'::text[], NULL, '{{}}'::text[], {q(reftxt)}, {i})")
        w("INSERT INTO cds_condition_content (condition_id, kind, text, generic_codes, line, severities, reference, sort_order)")
        w("SELECT cc.id, v.kind, v.text, v.gens, v.line, v.sevs, v.ref, v.ord FROM (VALUES\n  " + ",\n  ".join(content) +
          "\n) AS v(code, kind, text, gens, line, sevs, ref, ord) JOIN cds_conditions cc ON cc.code = v.code;")
    w("")
    w("-- Red flags")
    for code, title, crit, guidance, rk in RED_FLAGS:
        w(f"INSERT INTO cds_red_flags (code, title, criteria, guidance, reference, content_source) VALUES "
          f"({q(code)}, {q(title)}, {q(json.dumps(crit))}::jsonb, {q(guidance)}, {q(ref_text(rk) if rk else None)}, 'STARTER_DRAFT');")
    print("\n".join(out))


if __name__ == "__main__":
    main()
