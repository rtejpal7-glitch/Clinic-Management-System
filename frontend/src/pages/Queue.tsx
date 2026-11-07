import { useEffect, useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Appointment, Doctor, Page, Patient, QueueToken } from "../lib/types";
import { ageSex, time, todayIST } from "../lib/format";
import { navigate, useRoute } from "../lib/router";
import { Banner, Button, Combo, Empty, ErrorBanner, Loading, Modal, PageHeader, SelectField, StatusBadge, TextField, useAsync, useToast } from "../components/ui";
import { NowServing } from "./Dashboard";
import { startConsult } from "./Appointments";
import { IconPlus } from "../components/icons";

export function QueuePage() {
  const { can, user } = useAuth();
  const toast = useToast();
  const route = useRoute();
  const doctors = useAsync(() => api.get<Doctor[]>("/doctors"), []);
  const [doctorId, setDoctorId] = useState(user?.doctorId ?? "");
  const queue = useAsync(() => api.get<QueueToken[]>("/queue", { doctorId }), [doctorId]);
  const arrivals = useAsync(() => api.get<Appointment[]>("/appointments", { date: todayIST(), status: "SCHEDULED", doctorId }), [doctorId]);
  const [walkIn, setWalkIn] = useState(!!route.query.get("walkin"));

  useEffect(() => {
    const t = setInterval(() => { queue.reload(); arrivals.reload(); }, 15000);
    return () => clearInterval(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [doctorId]);

  const act = async (fn: () => Promise<unknown>, msg: string) => {
    try { await fn(); toast(msg); queue.reload(); arrivals.reload(); } catch (e) { toast((e as ApiError).message, "error"); }
  };
  const tokens = queue.data ?? [];
  const active = tokens.filter((t) => ["WAITING", "CALLED", "IN_CONSULTATION", "SKIPPED"].includes(t.status));
  const done = tokens.filter((t) => t.status === "DONE" || t.status === "CANCELLED");
  const shownDoctors = (doctors.data ?? []).filter((d) => !doctorId || d.id === doctorId);

  return (
    <>
      <PageHeader title="Waiting queue" subtitle="Tokens are issued at check-in and numbered per doctor each day. This page refreshes every 15 seconds."
        actions={can("appointment.manage") && <Button variant="primary" onClick={() => setWalkIn(true)}><IconPlus /> Add walk-in</Button>} />
      <div className="row" style={{ marginBottom: 16 }}>
        <select className="select" style={{ width: 240 }} value={doctorId} onChange={(e) => setDoctorId(e.target.value)} aria-label="Doctor">
          <option value="">All doctors</option>
          {doctors.data?.map((d) => <option key={d.id} value={d.id}>{d.displayName}</option>)}
        </select>
      </div>
      <ErrorBanner error={queue.error} />
      {shownDoctors.length > 0 && <NowServing doctors={shownDoctors} tokens={tokens} />}

      <div className="split">
        <section>
          <div className="section-title"><h2>In the queue</h2><span className="small muted">{active.filter((t) => t.status === "WAITING").length} waiting</span></div>
          <div className="table-wrap">
            {queue.loading && !queue.data ? <Loading /> : active.length === 0 ? <Empty title="Nobody is waiting">Check patients in from the list of today's arrivals, or add a walk-in.</Empty> : (
              <table className="data">
                <thead><tr><th>Token</th><th>Patient</th><th>Doctor</th><th>Waiting</th><th>Status</th><th className="right">Actions</th></tr></thead>
                <tbody>
                  {active.map((t) => (
                    <tr key={t.id}>
                      <td><span className={`token-num ${t.status === "WAITING" ? "waiting" : t.status === "CALLED" ? "called" : ""}`}>{t.tokenNumber}</span></td>
                      <td><a className="cell-main" href={`#/patients/${t.patientId}`}>{t.patientName}</a><div className="cell-sub">{t.patientNumber}{t.appointmentType === "WALK_IN" ? ", walk-in" : `, booked ${time(t.scheduledAt)}`}</div></td>
                      <td>{t.doctorName}</td>
                      <td className="num">{t.waitingMinutes != null ? `${t.waitingMinutes} min` : "—"}</td>
                      <td><StatusBadge status={t.status} /></td>
                      <td className="right">
                        <div className="row" style={{ justifyContent: "flex-end", gap: 4 }}>
                          {(t.status === "WAITING" || t.status === "SKIPPED") && can("queue.manage") && <Button size="sm" onClick={() => act(() => api.post(`/queue/${t.id}/call`), `Token ${t.tokenNumber} called`)}>Call</Button>}
                          {user?.doctorId === t.doctorId && t.status !== "IN_CONSULTATION" && <Button size="sm" variant="primary" onClick={() => startConsult(t.appointmentId, toast)}>Start consultation</Button>}
                          {t.status === "IN_CONSULTATION" && t.consultationId && can("clinical.read") && <Button size="sm" variant="primary" onClick={() => navigate(`/consultations/${t.consultationId}`)}>Continue notes</Button>}
                          {(t.status === "WAITING" || t.status === "CALLED") && can("queue.manage") && <Button size="sm" variant="ghost" onClick={() => act(() => api.post(`/queue/${t.id}/skip`), `Token ${t.tokenNumber} skipped`)}>Skip</Button>}
                        </div>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
          {done.length > 0 && <p className="small muted" style={{ marginTop: 10 }}>{done.filter((t) => t.status === "DONE").length} seen today, {done.filter((t) => t.status === "CANCELLED").length} cancelled.</p>}
        </section>
        <section>
          <div className="section-title"><h2>Expected today</h2></div>
          <div className="panel">
            {arrivals.loading && !arrivals.data ? <Loading /> : !arrivals.data?.length ? <Empty title="No pending arrivals" /> : (
              <ul style={{ listStyle: "none", margin: 0, padding: 0 }}>
                {arrivals.data.map((a) => (
                  <li key={a.id} style={{ display: "flex", gap: 10, alignItems: "center", padding: "10px 16px", borderBottom: "1px solid var(--rule)" }}>
                    <span className="tabular small" style={{ width: 62 }}>{time(a.startAt)}</span>
                    <div style={{ flex: 1, minWidth: 0 }}>
                      <div className="cell-main">{a.patientName}</div>
                      <div className="cell-sub">{a.doctorName}</div>
                    </div>
                    {can("appointment.manage") && <Button size="sm" onClick={() => act(() => api.post(`/appointments/${a.id}/check-in`), `${a.patientName} checked in`)}>Check in</Button>}
                  </li>
                ))}
              </ul>
            )}
          </div>
        </section>
      </div>
      {walkIn && <WalkInModal doctors={doctors.data ?? []} presetPatientId={route.query.get("walkin")} defaultDoctor={doctorId}
        onClose={() => { setWalkIn(false); if (route.query.get("walkin")) navigate("/queue"); }}
        onDone={() => { setWalkIn(false); queue.reload(); if (route.query.get("walkin")) navigate("/queue"); }} />}
    </>
  );
}

function WalkInModal({ doctors, presetPatientId, defaultDoctor, onClose, onDone }: {
  doctors: Doctor[]; presetPatientId: string | null; defaultDoctor: string; onClose: () => void; onDone: () => void;
}) {
  const toast = useToast();
  const [pt, setPt] = useState<Patient | null>(null);
  const [doctorId, setDoctorId] = useState(defaultDoctor || (doctors.length === 1 ? doctors[0].id : ""));
  const [reason, setReason] = useState("");
  const [err, setErr] = useState<string | null>(null);
  useEffect(() => { if (presetPatientId) api.get<Patient>(`/patients/${presetPatientId}`).then(setPt).catch(() => {}); }, [presetPatientId]);
  const save = async () => {
    try {
      const a = await api.post<Appointment>("/appointments/walk-in", { patientId: pt!.id, doctorId, reason: reason || undefined });
      toast(`Token ${a.tokenNumber} issued to ${a.patientName}`);
      onDone();
    } catch (e) { setErr((e as ApiError).message); }
  };
  return (
    <Modal title="Add walk-in patient" onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" disabled={!pt || !doctorId} onClick={save}>Issue token</Button></>}>
      {err && <Banner kind="error">{err}</Banner>}
      <div className="stack">
        {pt ? (
          <div className="row" style={{ justifyContent: "space-between" }}>
            <div><strong>{pt.fullName}</strong> <span className="muted small">{pt.patientNumber}, {ageSex(pt.ageYears, pt.gender)}</span></div>
            <Button size="sm" variant="ghost" onClick={() => setPt(null)}>Change</Button>
          </div>
        ) : (
          <Combo<Patient> label="Patient" placeholder="Search by name, mobile or patient ID" autoFocus minChars={2}
            search={(q) => api.get<Page<Patient>>("/patients", { q, size: 8 }).then((r) => r.items)}
            render={(p) => <><strong>{p.fullName}</strong> <span className="muted">{p.patientNumber}, {p.mobile}</span></>}
            onPick={setPt} hint={<>New patient? <a href="#/patients/new">Register first</a>.</>} />
        )}
        <SelectField label="Doctor" value={doctorId} onChange={setDoctorId} placeholder="Select doctor" options={doctors.map((d) => ({ value: d.id, label: d.displayName }))} />
        <TextField label="Reason for visit" value={reason} onChange={setReason} placeholder="Optional" />
      </div>
    </Modal>
  );
}
