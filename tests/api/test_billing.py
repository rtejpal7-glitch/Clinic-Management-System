"""Billing: invoices, calculations, payments, partial payments, refunds, reports."""
import datetime as dt
import re

from conftest import IST


def services(c):
    return {s["code"]: s for s in c.get("/billing/services").json()}


def test_invoice_generation_and_calculation(receptionist, new_patient):
    p = new_patient()
    s = services(receptionist)
    body = {"patientId": p["id"], "items": [{"serviceId": s["CONSULT-NEW"]["id"]},
                                            {"serviceId": s["OTH-CERT"]["id"]},
                                            {"description": "Dressing material", "quantity": 2, "unitPricePaise": 7500}],
            "discountPaise": 10000, "discountReason": "Senior citizen"}
    r = receptionist.post("/bills", body)
    assert r.status_code == 201, r.text
    b = r.json()
    assert re.match(r"^[A-Z]{2,6}-\d{4}-\d{6}$", b["invoiceNumber"])
    assert b["subtotalPaise"] == 50000 + 20000 + 15000
    assert b["discountPaise"] == 10000
    # tax only on the certificate line (18%) after its share of the discount
    cert = next(i for i in b["items"] if i["description"].startswith("Medical certificate"))
    assert cert["lineDiscountPaise"] == 10000 * 20000 // 85000
    assert cert["lineTaxPaise"] == round((20000 - cert["lineDiscountPaise"]) * 0.18)
    assert b["totalPaise"] == b["subtotalPaise"] - b["discountPaise"] + b["taxPaise"]
    assert sum(i["lineTotalPaise"] for i in b["items"]) == b["totalPaise"]
    assert b["status"] == "PENDING" and b["balancePaise"] == b["totalPaise"]


def test_discount_requires_reason_and_cannot_exceed_subtotal(receptionist, new_patient):
    p = new_patient()
    item = [{"description": "Consultation", "unitPricePaise": 30000}]
    assert receptionist.post("/bills", {"patientId": p["id"], "items": item, "discountPaise": 1000}).status_code == 400
    r = receptionist.post("/bills", {"patientId": p["id"], "items": item, "discountPaise": 40000, "discountReason": "too much"})
    assert r.status_code == 400


def test_partial_then_full_payment(receptionist, new_patient):
    p = new_patient()
    b = receptionist.post("/bills", {"patientId": p["id"], "items": [{"description": "Consultation", "unitPricePaise": 50000}]}).json()
    r1 = receptionist.post(f"/bills/{b['id']}/payments", {"amountPaise": 20000, "method": "CASH"})
    assert r1.status_code == 200 and r1.json()["status"] == "PARTIALLY_PAID" and r1.json()["balancePaise"] == 30000
    # overpayment rejected
    assert receptionist.post(f"/bills/{b['id']}/payments", {"amountPaise": 40000, "method": "UPI"}).status_code == 400
    r2 = receptionist.post(f"/bills/{b['id']}/payments", {"amountPaise": 30000, "method": "UPI", "reference": "UPI-REF-123"})
    assert r2.json()["status"] == "PAID" and r2.json()["balancePaise"] == 0
    assert [p["method"] for p in r2.json()["payments"]] == ["CASH", "UPI"]
    assert receptionist.post(f"/bills/{b['id']}/payments", {"amountPaise": 1, "method": "CASH"}).status_code == 400


def test_refund_requires_permission_and_reason(receptionist, admin, new_patient):
    p = new_patient()
    b = receptionist.post("/bills", {"patientId": p["id"], "items": [{"description": "Procedure", "unitPricePaise": 25000}]}).json()
    receptionist.post(f"/bills/{b['id']}/payments", {"amountPaise": 25000, "method": "CARD"})
    assert receptionist.post(f"/bills/{b['id']}/refunds", {"amountPaise": 5000, "method": "CASH", "notes": "Overcharged"}).status_code == 403
    assert admin.post(f"/bills/{b['id']}/refunds", {"amountPaise": 5000, "method": "CASH"}).status_code == 400
    r = admin.post(f"/bills/{b['id']}/refunds", {"amountPaise": 5000, "method": "CASH", "notes": "Procedure partly not done"})
    assert r.status_code == 200 and r.json()["refundedPaise"] == 5000 and r.json()["status"] == "PAID"
    full = admin.post(f"/bills/{b['id']}/refunds", {"amountPaise": 20000, "method": "CASH", "notes": "Full refund"})
    assert full.json()["status"] == "REFUNDED"
    assert admin.post(f"/bills/{b['id']}/refunds", {"amountPaise": 1, "method": "CASH", "notes": "more"}).status_code == 400


def test_cancel_only_unpaid(receptionist, admin, new_patient):
    p = new_patient()
    b = receptionist.post("/bills", {"patientId": p["id"], "items": [{"description": "X-ray", "unitPricePaise": 40000}]}).json()
    r = admin.post(f"/bills/{b['id']}/cancel", {"reason": "Created in error"})
    assert r.status_code == 200 and r.json()["status"] == "CANCELLED" and r.json()["balancePaise"] == 0
    b2 = receptionist.post("/bills", {"patientId": p["id"], "items": [{"description": "X-ray", "unitPricePaise": 40000}]}).json()
    receptionist.post(f"/bills/{b2['id']}/payments", {"amountPaise": 100, "method": "CASH"})
    assert admin.post(f"/bills/{b2['id']}/cancel", {"reason": "Created in error"}).status_code == 409


def test_appointment_cannot_be_billed_twice(receptionist, doctor, new_patient):
    p = new_patient()
    a = receptionist.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": doctor.doctor_id}).json()
    body = {"patientId": p["id"], "appointmentId": a["id"], "items": [{"description": "Consultation", "unitPricePaise": 50000}]}
    assert receptionist.post("/bills", body).status_code == 201
    r = receptionist.post("/bills", body)
    assert r.status_code == 409 and r.json()["error"]["code"] == "ALREADY_BILLED"
    assert receptionist.get(f"/appointments/{a['id']}").json()["billStatus"] == "PENDING"


def test_patient_billing_history_and_daily_summary(receptionist, admin, new_patient):
    p = new_patient()
    b = receptionist.post("/bills", {"patientId": p["id"], "items": [{"description": "Consultation", "unitPricePaise": 30000}]}).json()
    receptionist.post(f"/bills/{b['id']}/payments", {"amountPaise": 30000, "method": "UPI"})
    hist = receptionist.get(f"/patients/{p['id']}/bills").json()
    assert hist["total"] == 1 and hist["items"][0]["invoiceNumber"] == b["invoiceNumber"]
    today = dt.datetime.now(IST).date().isoformat()
    s = admin.get("/reports/billing/daily", params={"date": today}).json()
    assert s["billsIssued"] >= 1 and s["collectedByMethod"]["UPI"] >= 30000
    assert s["netCollectedPaise"] == s["collectedPaise"] - s["refundedPaise"]
    assert receptionist.get("/reports/billing/daily", params={"date": today}).status_code == 403


def test_billing_is_audited(receptionist, admin, new_patient):
    p = new_patient()
    b = receptionist.post("/bills", {"patientId": p["id"], "items": [{"description": "Consultation", "unitPricePaise": 30000}]}).json()
    receptionist.post(f"/bills/{b['id']}/payments", {"amountPaise": 30000, "method": "CASH"})
    actions = {l["action"] for l in admin.get("/audit-logs", params={"entityId": b["id"]}).json()["items"]}
    assert {"BILL_CREATE", "PAYMENT_RECORD"} <= actions
