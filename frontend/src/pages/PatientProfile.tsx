import { useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Allergy, Appointment, Page, Bill, Patient, TimelineEntry } from "../lib/types";
import { ageSex, dateOnly, dateTime, humanize, rupees, time } from "../lib/format";
import { navigate } from "../lib/router";
import {
  Banner, Button, Empty, ErrorBanner, Loading, Modal, PageHeader, SelectField, StatusBadge, Tabs, TextField, useAsync, useToast,
} from "../components/ui";
import { IconAlert, IconCalendar, IconReceipt } from "../components/icons";
import { AbhaPanel } from "./AbhaPanel";
import { BookAppointmentModal } from "./Appointments";

type Tab = "overview" | "history" | "appointments" | "billing" | "abdm" | "changes";

export function PatientProfilePage({ id, tab }: { id: string; tab: string }) {
  const { can } = useAuth();
  const p = useAsync(() => api.get<Patient>(`/patients/${id}`), [id]);
  const [booking, setBooking] = useState(false);
  const toast = useToast();
  const setTab = (t: Tab) => navigate(`/patients/${id}?tab=${t}`);
  const current = (tab as Tab) || "overview";

  if (p.loading && !p.data) return <Loading />;
  if (p.error) return <ErrorBanner error={p.error} />;
  const pt = p.data!;

  const walkIn = async () => {
    const docs = await api.get<{ id: string; displayName: string }[]>("/doctors");
    if (docs.length === 1) {
      try {
        const a = await api.post<Appointment>("/appointments/walk-in", { patientId: pt.id, doctorId: docs[0].id });
        toast(`Token ${a.tokenNumber} issued for ${docs[0].displayName}`);
        navigate("/queue");
      } catch (e) {
        toast((e as ApiError).message, "error");
      }
    } else navigate(`/queue?walkin=${pt.id}`);
  };

  const tabs: { id: Tab; label: string }[] = [{ id: "overview", label: "Overview" }];
  if (can("clinical.read")) tabs.push({ id: "history", label: "Medical history" });
  if (can("appointment.read")) tabs.push({ id: "appointments", label: "Appointments" });
  if (can("billing.read")) tabs.push({ id: "billing", label: "Billing" });
  if (can("abdm.read")) tabs.push({ id: "abdm", label: "ABHA & consent" });
  if (can("patient.history.read")) tabs.push({ id: "changes", label: "Change history" });

  return (
    <>
      <PageHeader
        title={pt.fullName}
        subtitle={<span className="tabular">{pt.patientNumber} &nbsp; {ageSex(pt.ageYears, pt.gender)} &nbsp; {pt.mobile}</span>}
        actions={
          <>
            {can("patient.update") && <Button onClick={() => navigate(`/patients/${pt.id}/edit`)}>Edit details</Button>}
            {can("billing.manage") && <Button onClick={() => navigate(`/bills/new?patientId=${pt.id}`)}><IconReceipt /> New bill</Button>}
            {can("appointment.manage") && <Button onClick={walkIn}>Add to queue (walk-in)</Button>}
            {can("appointment.manage") && <Button variant="primary" onClick={() => setBooking(true)}><IconCalendar /> Book appointment</Button>}
          </>
        }
      />
      <Tabs tabs={tabs} value={current} onChange={setTab} />
      {current === "overview" && <Overview p={pt} onChanged={p.reload} />}
      {current === "history" && <History patientId={pt.id} />}
      {current === "appointments" && <AppointmentsTab patientId={pt.id} />}
      {current === "billing" && <BillingTab patientId={pt.id} />}
      {current === "abdm" && <AbhaPanel patient={pt} onChanged={p.reload} />}
      {current === "changes" && <Changes patientId={pt.id} />}
      {booking && <BookAppointmentModal patient={pt} onClose={() => setBooking(false)} onBooked={() => { setBooking(false); setTab("appointments"); }} />}
    </>
  );
}

function Overview({ p, onChanged }: { p: Patient; onChanged: () => void }) {
  const { can } = useAuth();
  const toast = useToast();
  const toggleOptOut = async () => {
    const next = !p.networkOptOut;
    const r = await api.put<{ recordsWithdrawn: number }>(`/patients/${p.id}/network-opt-out`, { optOut: next });
    toast(next ? `Opt-out recorded${r.recordsWithdrawn ? `; ${r.recordsWithdrawn} contributed record(s) withdrawn` : ""}` : "Opt-out removed");
    onChanged();
  };
  return (
    <div className="split">
      <div className="panel">
        <div className="panel-body">
          <dl className="kv">
            <dt>Patient ID</dt><dd className="tabular">{p.patientNumber}</dd>
            <dt>Date of birth</dt><dd>{dateOnly(p.dateOfBirth)}{p.dobIsEstimated ? " (estimated)" : ""} — {p.ageYears} years</dd>
            <dt>Gender</dt><dd>{humanize(p.gender)}</dd>
            <dt>Blood group</dt><dd>{p.bloodGroup ?? "Not recorded"}</dd>
            <dt>Mobile</dt><dd className="tabular">{p.mobile}</dd>
            <dt>Email</dt><dd>{p.email ?? "—"}</dd>
            <dt>Address</dt><dd>{[p.addressLine, p.city, p.district, p.state, p.pincode].filter(Boolean).join(", ") || "—"}</dd>
            <dt>Emergency contact</dt><dd>{p.emergencyContactName ? `${p.emergencyContactName}${p.emergencyContactRelation ? ` (${p.emergencyContactRelation})` : ""}, ${p.emergencyContactPhone ?? ""}` : "—"}</dd>
            <dt>Marital status</dt><dd>{p.maritalStatus ? humanize(p.maritalStatus) : "—"}</dd>
            <dt>Occupation</dt><dd>{p.occupation ?? "—"}</dd>
            <dt>Preferred language</dt><dd>{p.preferredLanguage ?? "—"}</dd>
            <dt>ABHA</dt><dd>{p.abhaLinked ? <span className="badge b-green">Linked</span> : "Not linked"}</dd>
            <dt>Registered</dt><dd>{dateTime(p.createdAt)}</dd>
            <dt>Network data use</dt><dd>{p.networkOptOut ? <span className="badge b-grey">Opted out</span> : "Not opted out"}
              {can("patient.update") && <> <button className="btn btn-ghost btn-sm" onClick={toggleOptOut}>{p.networkOptOut ? "Remove opt-out" : "Record opt-out"}</button></>}
              <div className="small muted">Whether de-identified data from this patient's dermatology visits may be used in ClinicFlow network statistics</div></dd>
          </dl>
        </div>
      </div>
      {can("clinical.read") && <AllergiesPanel patientId={p.id} />}
    </div>
  );
}

export function AllergiesPanel({ patientId, compact }: { patientId: string; compact?: boolean }) {
  const { can } = useAuth();
  const toast = useToast();
  const list = useAsync(() => api.get<Allergy[]>(`/patients/${patientId}/allergies`), [patientId]);
  const [adding, setAdding] = useState(false);
  const [form, setForm] = useState({ substance: "", category: "MEDICATION", severity: "UNKNOWN", reaction: "" });
  const [err, setErr] = useState<Record<string, string>>({});
  const active = (list.data ?? []).filter((a) => a.status === "ACTIVE");
  const save = async () => {
    try {
      await api.post(`/patients/${patientId}/allergies`, form);
      toast("Allergy recorded");
      setAdding(false);
      setForm({ substance: "", category: "MEDICATION", severity: "UNKNOWN", reaction: "" });
      list.reload();
    } catch (e) {
      setErr((e as ApiError).fields);
    }
  };
  const mark = async (a: Allergy, status: string) => {
    await api.put(`/patients/${patientId}/allergies/${a.id}`, { status });
    list.reload();
  };
  return (
    <div className="panel">
      <div className="panel-head">
        <h3>Allergies</h3>
        {can("clinical.write") && <Button size="sm" variant="ghost" onClick={() => setAdding(true)}>Add allergy</Button>}
      </div>
      <div className="panel-body" style={{ paddingTop: 10 }}>
        {list.loading ? <Loading /> : active.length === 0 ? (
          <p className="muted small" style={{ margin: 0 }}>No known allergies recorded. Ask the patient and record any here.</p>
        ) : (
          <ul style={{ margin: 0, paddingLeft: 0, listStyle: "none" }} className="stack">
            {active.map((a) => (
              <li key={a.id} style={{ display: "flex", gap: 10, alignItems: "flex-start" }}>
                <IconAlert width={16} height={16} style={{ color: "var(--red)", marginTop: 3, flexShrink: 0 }} />
                <div style={{ flex: 1 }}>
                  <strong>{a.substance}</strong> <span className="muted small">{humanize(a.category)}, {humanize(a.severity).toLowerCase()}</span>
                  {a.reaction && <div className="small">{a.reaction}</div>}
                </div>
                {can("clinical.write") && !compact && (
                  <Button size="sm" variant="ghost" onClick={() => mark(a, "ENTERED_IN_ERROR")} title="Recorded by mistake">Remove</Button>
                )}
              </li>
            ))}
          </ul>
        )}
      </div>
      {adding && (
        <Modal title="Record allergy" onClose={() => setAdding(false)} footer={<><Button onClick={() => setAdding(false)}>Cancel</Button><Button variant="primary" onClick={save}>Save allergy</Button></>}>
          <div className="grid g2">
            <TextField label="Substance" required value={form.substance} onChange={(v) => setForm({ ...form, substance: v })} error={err.substance} className="span-all" placeholder="e.g. Penicillin" />
            <SelectField label="Category" value={form.category} onChange={(v) => setForm({ ...form, category: v })} options={["MEDICATION", "FOOD", "ENVIRONMENT", "OTHER"]} />
            <SelectField label="Severity" value={form.severity} onChange={(v) => setForm({ ...form, severity: v })} options={["MILD", "MODERATE", "SEVERE", "UNKNOWN"]} />
            <TextField label="Reaction" value={form.reaction} onChange={(v) => setForm({ ...form, reaction: v })} className="span-all" placeholder="e.g. Rash, breathlessness" />
          </div>
        </Modal>
      )}
    </div>
  );
}

function History({ patientId }: { patientId: string }) {
  const tl = useAsync(() => api.get<TimelineEntry[]>(`/patients/${patientId}/timeline`), [patientId]);
  if (tl.loading) return <Loading />;
  if (tl.error) return <ErrorBanner error={tl.error} />;
  if (!tl.data?.length) return <Empty title="No consultations yet">Consultations appear here once a doctor sees this patient.</Empty>;
  return (
    <div className="panel"><div className="panel-body">
      <ul className="timeline">
        {tl.data.map((e) => (
          <li key={e.consultationId}>
            <div>
              <div className="tl-date">{dateOnly(e.startedAt)}</div>
              <div className="small muted">{time(e.startedAt)}</div>
              <div style={{ marginTop: 6 }}><StatusBadge status={e.status} /></div>
            </div>
            <div className="stack" style={{ gap: 8 }}>
              <div className="row" style={{ justifyContent: "space-between" }}>
                <div><strong>{e.chiefComplaint ?? "No chief complaint recorded"}</strong> <span className="muted small">with {e.doctorName}</span></div>
                <a href={`#/consultations/${e.consultationId}`} className="small">Open consultation</a>
              </div>
              {e.diagnoses.length > 0 && <div className="chips">{e.diagnoses.map((d) => <span className="chip" key={d}>{d}</span>)}</div>}
              {e.medicines.length > 0 && <div className="small"><span className="muted">Medicines: </span>{e.medicines.join("; ")}</div>}
              {e.investigations.length > 0 && <div className="small"><span className="muted">Investigations: </span>{e.investigations.join(", ")}</div>}
              {e.vitals && (
                <div className="small tabular muted">
                  {e.vitals.bpSystolic ? `BP ${e.vitals.bpSystolic}/${e.vitals.bpDiastolic}  ` : ""}
                  {e.vitals.pulse ? `Pulse ${e.vitals.pulse}  ` : ""}
                  {e.vitals.temperatureC ? `Temp ${e.vitals.temperatureC} °C  ` : ""}
                  {e.vitals.spo2 ? `SpO₂ ${e.vitals.spo2}%  ` : ""}
                  {e.vitals.weightKg ? `Wt ${e.vitals.weightKg} kg` : ""}
                </div>
              )}
              {e.followUpDate && <div className="small">Follow-up advised on {dateOnly(e.followUpDate)}</div>}
            </div>
          </li>
        ))}
      </ul>
    </div></div>
  );
}

function AppointmentsTab({ patientId }: { patientId: string }) {
  const list = useAsync(() => api.get<Appointment[]>(`/patients/${patientId}/appointments`), [patientId]);
  if (list.loading) return <Loading />;
  if (!list.data?.length) return <Empty title="No appointments yet" />;
  return (
    <div className="table-wrap">
      <table className="data">
        <thead><tr><th>Date</th><th>Doctor</th><th>Type</th><th>Reason</th><th>Status</th><th /></tr></thead>
        <tbody>
          {list.data.map((a) => (
            <tr key={a.id}>
              <td className="nowrap num">{dateTime(a.startAt)}</td>
              <td>{a.doctorName}</td>
              <td><StatusBadge status={a.appointmentType === "SCHEDULED" ? undefined : a.appointmentType} />{a.appointmentType === "SCHEDULED" && "Scheduled"}</td>
              <td>{a.reason ?? <span className="muted">—</span>}{a.cancellationReason && <div className="cell-sub">Cancelled: {a.cancellationReason}</div>}</td>
              <td><StatusBadge status={a.status} /></td>
              <td className="right">{a.consultationId && <a href={`#/consultations/${a.consultationId}`} className="small">Consultation</a>}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function BillingTab({ patientId }: { patientId: string }) {
  const list = useAsync(() => api.get<Page<Bill>>(`/patients/${patientId}/bills`), [patientId]);
  if (list.loading) return <Loading />;
  if (!list.data?.items.length) return <Empty title="No bills yet" />;
  const due = list.data.items.reduce((s, b) => s + b.balancePaise, 0);
  return (
    <>
      {due > 0 && <Banner kind="warn">Outstanding balance across bills: <strong>{rupees(due)}</strong></Banner>}
      <div className="table-wrap">
        <table className="data">
          <thead><tr><th>Invoice</th><th>Date</th><th className="right">Total</th><th className="right">Paid</th><th className="right">Balance</th><th>Status</th></tr></thead>
          <tbody>
            {list.data.items.map((b) => (
              <tr key={b.id} className="clickable" onClick={() => navigate(`/bills/${b.id}`)}>
                <td className="num cell-main">{b.invoiceNumber}</td>
                <td className="nowrap">{dateOnly(b.createdAt)}</td>
                <td className="num right">{rupees(b.totalPaise)}</td>
                <td className="num right">{rupees(b.paidPaise - b.refundedPaise)}</td>
                <td className="num right">{rupees(b.balancePaise)}</td>
                <td><StatusBadge status={b.status} /></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </>
  );
}

function Changes({ patientId }: { patientId: string }) {
  const list = useAsync(() => api.get<{ id: number; changedAt: string; changedBy?: string; fieldName: string; oldValue?: string; newValue?: string; reason?: string }[]>(`/patients/${patientId}/history`), [patientId]);
  if (list.loading) return <Loading />;
  if (!list.data?.length) return <Empty title="No changes since registration" />;
  return (
    <div className="table-wrap">
      <table className="data">
        <thead><tr><th>When</th><th>By</th><th>Field</th><th>Before</th><th>After</th><th>Reason</th></tr></thead>
        <tbody>
          {list.data.map((h) => (
            <tr key={h.id}>
              <td className="nowrap">{dateTime(h.changedAt)}</td>
              <td>{h.changedBy}</td>
              <td>{humanize(h.fieldName)}</td>
              <td className="muted">{h.oldValue ?? "—"}</td>
              <td>{h.newValue ?? "—"}</td>
              <td className="muted">{h.reason ?? "—"}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
