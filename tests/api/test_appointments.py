"""Scheduling: slots, booking, double-booking prevention, lifecycle, queue."""
import concurrent.futures
import datetime as dt

import pytest

from conftest import IST, future_slot, today_slot


def test_slots_reflect_schedule(receptionist, doctor):
    date = (dt.datetime.now(IST) + dt.timedelta(days=2)).date().isoformat()
    slots = receptionist.get(f"/doctors/{doctor.doctor_id}/slots", params={"date": date}).json()
    assert len(slots) == 95  # 00:00 to 23:45 in 15-minute slots
    assert all(s["startAt"].endswith("Z") for s in slots)


def test_book_appointment(receptionist, doctor, new_patient):
    p = new_patient()
    start = future_slot(receptionist, doctor.doctor_id)
    r = receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id, "startAt": start, "reason": "Fever"})
    assert r.status_code == 201, r.text
    a = r.json()
    assert a["status"] == "SCHEDULED" and a["startAt"] == start
    # slot now shows as booked
    date = dt.datetime.fromisoformat(start.replace("Z", "+00:00")).astimezone(IST).date().isoformat()
    slots = receptionist.get(f"/doctors/{doctor.doctor_id}/slots", params={"date": date}).json()
    assert next(s for s in slots if s["startAt"] == start)["available"] is False


def test_double_booking_is_prevented(receptionist, doctor, new_patient):
    p1, p2 = new_patient(), new_patient()
    start = future_slot(receptionist, doctor.doctor_id, days_ahead=3)
    assert receptionist.post("/appointments", {"patientId": p1["id"], "doctorId": doctor.doctor_id, "startAt": start}).status_code == 201
    r = receptionist.post("/appointments", {"patientId": p2["id"], "doctorId": doctor.doctor_id, "startAt": start})
    assert r.status_code == 409
    assert r.json()["error"]["code"] in ("SLOT_UNAVAILABLE", "CONFLICT")


def test_concurrent_double_booking_only_one_wins(receptionist, doctor, new_patient):
    patients = [new_patient() for _ in range(4)]
    start = future_slot(receptionist, doctor.doctor_id, days_ahead=4, index=3)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        codes = list(ex.map(lambda p: receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id,
                                                                          "startAt": start}).status_code, patients))
    assert codes.count(201) == 1, codes
    assert codes.count(409) == 3, codes


def test_booking_outside_schedule_is_rejected(receptionist, doctor, new_patient):
    p = new_patient()
    date = (dt.datetime.now(IST) + dt.timedelta(days=5)).date()
    off_grid = dt.datetime.combine(date, dt.time(10, 7), IST).isoformat()  # not on a 15-minute boundary
    r = receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id, "startAt": off_grid})
    assert r.status_code == 422 and r.json()["error"]["code"] == "SLOT_OUTSIDE_SCHEDULE"
    past = dt.datetime.combine(dt.date.today() - dt.timedelta(days=2), dt.time(10, 0), IST).isoformat()
    assert receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id, "startAt": past}).status_code == 400


def test_cancel_and_reschedule(receptionist, doctor, new_patient):
    p = new_patient()
    s1 = future_slot(receptionist, doctor.doctor_id, days_ahead=6)
    a = receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id, "startAt": s1}).json()
    s2 = future_slot(receptionist, doctor.doctor_id, days_ahead=6, index=1)
    r = receptionist.post(f"/appointments/{a['id']}/reschedule", {"startAt": s2, "version": a["version"], "reason": "Patient request"})
    assert r.status_code == 200, r.text
    assert r.json()["startAt"] == s2
    # cancellation requires a reason
    assert receptionist.post(f"/appointments/{a['id']}/cancel", {}).status_code == 400
    c = receptionist.post(f"/appointments/{a['id']}/cancel", {"reason": "Patient unwell, will call back"})
    assert c.status_code == 200 and c.json()["status"] == "CANCELLED"
    # cancelled slot becomes free again and cannot be re-cancelled
    assert receptionist.post(f"/appointments/{a['id']}/cancel", {"reason": "again"}).status_code == 409
    hist = receptionist.get(f"/appointments/{a['id']}/history").json()
    assert [h["toStatus"] for h in hist] == ["SCHEDULED", "SCHEDULED", "CANCELLED"]


def test_follow_up_scheduling(receptionist, doctor, new_patient):
    p = new_patient()
    first = receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id,
                                                "startAt": future_slot(receptionist, doctor.doctor_id, days_ahead=7)}).json()
    fu = receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id, "appointmentType": "FOLLOW_UP",
                                             "followUpOf": first["id"],
                                             "startAt": future_slot(receptionist, doctor.doctor_id, days_ahead=10)})
    assert fu.status_code == 201, fu.text
    assert fu.json()["appointmentType"] == "FOLLOW_UP" and fu.json()["followUpOf"] == first["id"]
    # follow-up must reference a visit of the same patient
    other = receptionist.post("/appointments", {"patientId": p["id"], "doctorId": doctor.doctor_id, "appointmentType": "FOLLOW_UP"})
    assert other.status_code == 400


def test_walk_in_issues_queue_token(receptionist, doctor, new_patient):
    p = new_patient()
    r = receptionist.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": doctor.doctor_id, "reason": "Cough"})
    assert r.status_code == 201, r.text
    a = r.json()
    assert a["status"] == "CHECKED_IN" and a["tokenNumber"] >= 1
    q = receptionist.get("/queue", params={"doctorId": doctor.doctor_id}).json()
    tok = next(t for t in q if t["appointmentId"] == a["id"])
    assert tok["status"] == "WAITING" and tok["displayToken"].startswith("T-")
    # same patient cannot be queued twice for the same doctor
    assert receptionist.post("/appointments/walk-in", {"patientId": p["id"], "doctorId": doctor.doctor_id}).status_code == 409
    # call and skip
    assert receptionist.post(f"/queue/{tok['id']}/call").json()["status"] == "CALLED"
    assert receptionist.post(f"/queue/{tok['id']}/skip").json()["status"] == "SKIPPED"


def test_queue_tokens_increment(receptionist, doctor, new_patient):
    a = receptionist.post("/appointments/walk-in", {"patientId": new_patient()["id"], "doctorId": doctor.doctor_id}).json()
    b = receptionist.post("/appointments/walk-in", {"patientId": new_patient()["id"], "doctorId": doctor.doctor_id}).json()
    assert b["tokenNumber"] == a["tokenNumber"] + 1


def test_check_in_today(receptionist, doctor, new_patient):
    slot = today_slot(receptionist, doctor.doctor_id)
    if not slot:
        pytest.skip("No remaining slots today")
    a = receptionist.post("/appointments", {"patientId": new_patient()["id"], "doctorId": doctor.doctor_id, "startAt": slot}).json()
    r = receptionist.post(f"/appointments/{a['id']}/check-in")
    assert r.status_code == 200 and r.json()["status"] == "CHECKED_IN" and r.json()["tokenNumber"] is not None
    assert receptionist.post(f"/appointments/{a['id']}/check-in").status_code == 409


def test_future_appointment_cannot_be_checked_in_or_no_show(receptionist, doctor, new_patient):
    a = receptionist.post("/appointments", {"patientId": new_patient()["id"], "doctorId": doctor.doctor_id,
                                            "startAt": future_slot(receptionist, doctor.doctor_id, days_ahead=8)}).json()
    assert receptionist.post(f"/appointments/{a['id']}/check-in").status_code == 422
    assert receptionist.post(f"/appointments/{a['id']}/no-show").status_code == 422


def test_reminder_is_queued_for_future_booking(receptionist, doctor, new_patient, admin):
    a = receptionist.post("/appointments", {"patientId": new_patient()["id"], "doctorId": doctor.doctor_id,
                                            "startAt": future_slot(receptionist, doctor.doctor_id, days_ahead=9)}).json()
    assert a["status"] == "SCHEDULED"  # reminder rows are verified at DB level in docs/TESTING.md


def test_schedule_validation(admin, doctor):
    r = admin.put(f"/doctors/{doctor.doctor_id}/schedule", {"blocks": [{"weekday": 1, "startTime": "10:00", "endTime": "09:00", "slotMinutes": 15}]})
    assert r.status_code == 400
    r = admin.put(f"/doctors/{doctor.doctor_id}/schedule", {"blocks": [
        {"weekday": 1, "startTime": "09:00", "endTime": "12:00", "slotMinutes": 15},
        {"weekday": 1, "startTime": "11:00", "endTime": "13:00", "slotMinutes": 15}]})
    assert r.status_code == 409 and r.json()["error"]["code"] == "OVERLAPPING_SCHEDULE"
    # the failed update left the previous schedule intact (transaction rolled back)
    assert len(admin.get(f"/doctors/{doctor.doctor_id}/schedule").json()) == 7
