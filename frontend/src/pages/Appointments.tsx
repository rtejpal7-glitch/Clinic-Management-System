import { useEffect, useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Appointment, Doctor, Page, Patient, Slot } from "../lib/types";
import { addDays, ageSex, dateOnly, humanize, time, todayIST, weekdayName } from "../lib/format";
import { navigate, useRoute } from "../lib/router";
import {
  Banner, Button, Combo, Empty, ErrorBanner, Loading, Modal, PageHeader, SelectField, StatusBadge, TextArea, TextField, useAsync, useToast,
} from "../components/ui";
import { IconPlus } from "../components/icons";

export function AppointmentsPage() {
  const { can, user } = useAuth();
  const route = useRoute();
  const toast = useToast();
  const [date, setDate] = useState(todayIST());
  const doctors = useAsync(() => api.get<Doctor[]>("/doctors"), []);
  const [doctorId, setDoctorId] = useState(user?.doctorId ?? "");
  const [status, setStatus] = useState("");
  const list = useAsync(() => api.get<Appointment[]>("/appointments", { date, doctorId, status }), [date, doctorId, status]);
  const [booking, setBooking] = useState(route.query.get("book") === "1");
  const [resched, setResched] = useState<Appointment | null>(null);
  const [cancelling, setCancelling] = useState<Appointment | null>(null);

  const act = async (fn: () => Promise<unknown>, msg: string) => {
    try { await fn(); toast(msg); list.reload(); } catch (e) { toast((e as ApiError).message, "error"); }
  };
  const isToday = date === todayIST();
  const counts = (list.data ?? []).reduce<Record<string, number>>((m, a) => ((m[a.status] = (m[a.status] ?? 0) + 1), m), {});

  return (
    <>
      <PageHeader title="Appointments" subtitle={`${weekdayName(date, "long")}, ${dateOnly(date)}${isToday ? " (today)" : ""}`}
        actions={can("appointment.manage") && <Button variant="primary" onClick={() => setBooking(true)}><IconPlus /> Book appointment</Button>} />
      <div className="row" style={{ marginBottom: 14 }}>
        <Button size="sm" onClick={() => setDate(addDays(date, -1))} aria-label="Previous day">Previous</Button>
        <input type="date" className="input" style={{ width: 160 }} value={date} onChange={(e) => e.target.value && setDate(e.target.value)} aria-label="Date" />
        <Button size="sm" onClick={() => setDate(addDays(date, 1))} aria-label="Next day">Next</Button>
        {!isToday && <Button size="sm" variant="ghost" onClick={() => setDate(todayIST())}>Today</Button>}
        <select className="select" style={{ width: 220 }} value={doctorId} onChange={(e) => setDoctorId(e.target.value)} aria-label="Doctor">
          <option value="">All doctors</option>
          {doctors.data?.map((d) => <option key={d.id} value={d.id}>{d.displayName}</option>)}
        </select>
        <select className="select" style={{ width: 170 }} value={status} onChange={(e) => setStatus(e.target.value)} aria-label="Status">
          <option value="">Any status</option>
          {["SCHEDULED", "CHECKED_IN", "IN_CONSULTATION", "COMPLETED", "CANCELLED", "NO_SHOW"].map((s) => <option key={s} value={s}>{humanize(s)}</option>)}
        </select>
        <span className="small muted">
          {Object.entries(counts).map(([k, v]) => `${v} ${humanize(k).toLowerCase()}`).join(", ") || "No appointments"}
        </span>
      </div>
      <ErrorBanner error={list.error} />
      <div className="table-wrap">
        {list.loading && !list.data ? <Loading /> : !list.data?.length ? (
          <Empty title="No appointments on this day">{can("appointment.manage") && <p style={{ marginTop: 8 }}><a href="#" onClick={(e) => { e.preventDefault(); setBooking(true); }}>Book one</a></p>}</Empty>
        ) : (
          <table className="data">
            <thead><tr><th>Time</th><th>Token</th><th>Patient</th><th>Doctor</th><th>Reason</th><th>Status</th><th>Bill</th><th className="right">Actions</th></tr></thead>
            <tbody>
              {list.data.map((a) => (
                <tr key={a.id}>
                  <td className="num nowrap">{a.appointmentType === "WALK_IN" ? <StatusBadge status="WALK_IN" label="Walk-in" /> : time(a.startAt)}
                    {a.appointmentType === "FOLLOW_UP" && <div className="cell-sub">Follow-up</div>}</td>
                  <td className="num">{a.tokenNumber ? <span className="token-num" style={{ fontSize: "1rem" }}>{a.tokenNumber}</span> : <span className="muted">—</span>}</td>
                  <td><a href={`#/patients/${a.patientId}`} className="cell-main">{a.patientName}</a><div className="cell-sub">{a.patientNumber}, {ageSex(a.patientAgeYears, a.patientGender)}</div></td>
                  <td>{a.doctorName}</td>
                  <td className="small">{a.reason ?? <span className="muted">—</span>}</td>
                  <td><StatusBadge status={a.status} /></td>
                  <td>{a.billId ? <a href={`#/bills/${a.billId}`}><StatusBadge status={a.billStatus} /></a> : <span className="muted small">—</span>}</td>
                  <td className="right nowrap">
                    <div className="row" style={{ justifyContent: "flex-end", gap: 4 }}>
                      {a.status === "SCHEDULED" && isToday && can("appointment.manage") && <Button size="sm" variant="primary" onClick={() => act(() => api.post(`/appointments/${a.id}/check-in`), `${a.patientName} checked in`)}>Check in</Button>}
                      {a.status === "CHECKED_IN" && user?.doctorId === a.doctorId && <Button size="sm" variant="primary" onClick={() => startConsult(a.id, toast)}>Start consultation</Button>}
                      {a.consultationId && can("clinical.read") && <Button size="sm" onClick={() => navigate(`/consultations/${a.consultationId}`)}>Open notes</Button>}
                      {!a.billId && ["CHECKED_IN", "IN_CONSULTATION", "COMPLETED"].includes(a.status) && can("billing.manage") && <Button size="sm" onClick={() => navigate(`/bills/new?patientId=${a.patientId}&appointmentId=${a.id}`)}>Bill</Button>}
                      {a.status === "SCHEDULED" && can("appointment.manage") && <Button size="sm" variant="ghost" onClick={() => setResched(a)}>Reschedule</Button>}
                      {a.status === "SCHEDULED" && new Date(a.startAt) < new Date() && can("appointment.manage") && <Button size="sm" variant="ghost" onClick={() => act(() => api.post(`/appointments/${a.id}/no-show`), "Marked as no-show")}>No-show</Button>}
                      {(a.status === "SCHEDULED" || a.status === "CHECKED_IN") && can("appointment.manage") && <Button size="sm" variant="ghost" onClick={() => setCancelling(a)}>Cancel</Button>}
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      {booking && <BookAppointmentModal initialDate={date} initialDoctorId={doctorId} onClose={() => setBooking(false)} onBooked={(a) => { setBooking(false); setDate(a.startAt ? dateFromIso(a.startAt) : date); list.reload(); }} />}
      {resched && <RescheduleModal appt={resched} onClose={() => setResched(null)} onDone={() => { setResched(null); list.reload(); }} />}
      {cancelling && <CancelModal appt={cancelling} onClose={() => setCancelling(null)} onDone={() => { setCancelling(null); list.reload(); }} />}
    </>
  );
}

const dateFromIso = (iso: string) => new Intl.DateTimeFormat("en-CA", { timeZone: "Asia/Kolkata" }).format(new Date(iso));

export async function startConsult(appointmentId: string, toast: (t: string, k?: "ok" | "error") => void) {
  try {
    const c = await api.post<{ id: string }>(`/appointments/${appointmentId}/start-consultation`);
    navigate(`/consultations/${c.id}`);
  } catch (e) {
    toast((e as ApiError).message, "error");
  }
}

export function SlotPicker({ doctorId, date, value, onChange }: { doctorId: string; date: string; value: string; onChange: (s: string) => void }) {
  const slots = useAsync(() => (doctorId && date ? api.get<Slot[]>(`/doctors/${doctorId}/slots`, { date }) : Promise.resolve([] as Slot[])), [doctorId, date]);
  if (!doctorId) return <p className="small muted">Select a doctor to see free slots.</p>;
  if (slots.loading) return <Loading label="Loading slots" />;
  if (!slots.data?.length) return <p className="small muted">The doctor has no working hours on {weekdayName(date, "long")}. Choose another date.</p>;
  const free = slots.data.filter((s) => s.available).length;
  return (
    <div>
      <p className="small muted" style={{ marginBottom: 8 }}>{free} of {slots.data.length} slots free. Crossed-out slots are booked or past.</p>
      <div className="slot-grid" role="group" aria-label="Available slots">
        {slots.data.map((s) => (
          <button key={s.startAt} type="button" className="slot" disabled={!s.available} aria-pressed={value === s.startAt}
            onClick={() => onChange(s.startAt)} title={s.reason ? humanize(s.reason) : "Free"}>{time(s.startAt)}</button>
        ))}
      </div>
    </div>
  );
}

export function BookAppointmentModal({ patient, initialDate, initialDoctorId, followUpOf, onClose, onBooked }: {
  patient?: Patient; initialDate?: string; initialDoctorId?: string; followUpOf?: string; onClose: () => void; onBooked: (a: Appointment) => void;
}) {
  const toast = useToast();
  const doctors = useAsync(() => api.get<Doctor[]>("/doctors"), []);
  const [pt, setPt] = useState<Patient | undefined>(patient);
  const [doctorId, setDoctorId] = useState(initialDoctorId ?? "");
  const [date, setDate] = useState(initialDate && initialDate >= todayIST() ? initialDate : todayIST());
  const [slot, setSlot] = useState("");
  const [reason, setReason] = useState("");
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  useEffect(() => { if (!doctorId && doctors.data?.length === 1) setDoctorId(doctors.data[0].id); }, [doctors.data, doctorId]);
  useEffect(() => setSlot(""), [doctorId, date]);

  const book = async () => {
    setBusy(true); setErr(null);
    try {
      const a = await api.post<Appointment>("/appointments", {
        patientId: pt!.id, doctorId, startAt: slot, reason: reason || undefined,
        appointmentType: followUpOf ? "FOLLOW_UP" : "SCHEDULED", followUpOf,
      });
      toast(`Booked ${a.patientName} with ${a.doctorName} at ${time(a.startAt)}, ${dateOnly(a.startAt)}`);
      onBooked(a);
    } catch (e) {
      const ae = e as ApiError;
      setErr(ae.message);
      if (ae.code === "SLOT_UNAVAILABLE") setSlot("");
    } finally { setBusy(false); }
  };

  return (
    <Modal title={followUpOf ? "Book follow-up" : "Book appointment"} wide onClose={onClose}
      footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" busy={busy} disabled={!pt || !doctorId || !slot} onClick={book}>Book appointment</Button></>}>
      {err && <Banner kind="error">{err}</Banner>}
      <div className="stack">
        {pt ? (
          <div className="row" style={{ justifyContent: "space-between" }}>
            <div><strong>{pt.fullName}</strong> <span className="muted small">{pt.patientNumber}, {ageSex(pt.ageYears, pt.gender)}, {pt.mobile}</span></div>
            {!patient && <Button size="sm" variant="ghost" onClick={() => setPt(undefined)}>Change patient</Button>}
          </div>
        ) : (
          <Combo<Patient> label="Patient" placeholder="Search by name, mobile or patient ID" autoFocus minChars={2}
            search={(q) => api.get<Page<Patient>>("/patients", { q, size: 8 }).then((r) => r.items)}
            render={(p) => <><strong>{p.fullName}</strong> <span className="muted">{p.patientNumber}, {ageSex(p.ageYears, p.gender)}, {p.mobile}</span></>}
            onPick={setPt} hint={<>Not registered yet? <a href="#/patients/new">Register the patient first</a>.</>} />
        )}
        <div className="grid g3">
          <SelectField label="Doctor" required value={doctorId} onChange={setDoctorId} placeholder="Select doctor"
            options={(doctors.data ?? []).map((d) => ({ value: d.id, label: `${d.displayName}${d.specialization ? ` (${d.specialization})` : ""}` }))} />
          <TextField label="Date" type="date" value={date} onChange={(v) => v && setDate(v)} min={todayIST()} />
          <TextField label="Reason for visit" value={reason} onChange={setReason} placeholder="Optional" />
        </div>
        <SlotPicker doctorId={doctorId} date={date} value={slot} onChange={setSlot} />
      </div>
    </Modal>
  );
}

function RescheduleModal({ appt, onClose, onDone }: { appt: Appointment; onClose: () => void; onDone: () => void }) {
  const toast = useToast();
  const [date, setDate] = useState(dateFromIso(appt.startAt) < todayIST() ? todayIST() : dateFromIso(appt.startAt));
  const [slot, setSlot] = useState("");
  const [reason, setReason] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const save = async () => {
    try {
      await api.post(`/appointments/${appt.id}/reschedule`, { startAt: slot, reason, version: appt.version });
      toast(`Moved to ${time(slot)}, ${dateOnly(slot)}`);
      onDone();
    } catch (e) { setErr((e as ApiError).message); }
  };
  return (
    <Modal title={`Reschedule ${appt.patientName}`} wide onClose={onClose}
      footer={<><Button onClick={onClose}>Keep current time</Button><Button variant="primary" disabled={!slot} onClick={save}>Move appointment</Button></>}>
      {err && <Banner kind="error">{err}</Banner>}
      <p className="small">Currently {time(appt.startAt)} on {dateOnly(appt.startAt)} with {appt.doctorName}.</p>
      <div className="grid g2" style={{ marginBottom: 14 }}>
        <TextField label="New date" type="date" value={date} onChange={(v) => { if (v) { setDate(v); setSlot(""); } }} min={todayIST()} />
        <TextField label="Reason" value={reason} onChange={setReason} placeholder="e.g. Patient request" />
      </div>
      <SlotPicker doctorId={appt.doctorId} date={date} value={slot} onChange={setSlot} />
    </Modal>
  );
}

function CancelModal({ appt, onClose, onDone }: { appt: Appointment; onClose: () => void; onDone: () => void }) {
  const toast = useToast();
  const [reason, setReason] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const save = async () => {
    try { await api.post(`/appointments/${appt.id}/cancel`, { reason }); toast("Appointment cancelled"); onDone(); }
    catch (e) { setErr((e as ApiError).message); }
  };
  return (
    <Modal title="Cancel appointment" onClose={onClose}
      footer={<><Button onClick={onClose}>Keep appointment</Button><Button variant="danger" disabled={reason.trim().length < 3} onClick={save}>Cancel appointment</Button></>}>
      {err && <Banner kind="error">{err}</Banner>}
      <p>{appt.patientName} with {appt.doctorName}{appt.appointmentType !== "WALK_IN" ? ` at ${time(appt.startAt)}, ${dateOnly(appt.startAt)}` : ""}.</p>
      <TextArea label="Reason" required value={reason} onChange={setReason} rows={2} hint="Recorded in the appointment history" />
    </Modal>
  );
}
