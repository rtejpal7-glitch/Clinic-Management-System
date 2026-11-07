import { useEffect, useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Doctor, Medication, Service, Settings, UserRow } from "../lib/types";
import { humanize, rupees, toPaise } from "../lib/format";
import { Banner, Button, Loading, Modal, PageHeader, SelectField, Tabs, TextField, useAsync, useToast } from "../components/ui";
import { IconPlus } from "../components/icons";

type Tab = "clinic" | "doctors" | "services" | "medicines";

export function SettingsPage() {
  const { can } = useAuth();
  const [tab, setTab] = useState<Tab>("clinic");
  const tabs: { id: Tab; label: string }[] = [{ id: "clinic", label: "Clinic profile" }];
  if (can("doctor.manage")) tabs.push({ id: "doctors", label: "Doctors" });
  if (can("catalogue.manage")) tabs.push({ id: "services", label: "Billable services" }, { id: "medicines", label: "Medicine catalogue" });
  return (
    <>
      <PageHeader title="Clinic settings" />
      <Tabs tabs={tabs} value={tab} onChange={setTab} />
      {tab === "clinic" && <ClinicTab />}
      {tab === "doctors" && <DoctorsTab />}
      {tab === "services" && <ServicesTab />}
      {tab === "medicines" && <MedicinesTab />}
    </>
  );
}

function ClinicTab() {
  const toast = useToast();
  const s = useAsync(() => api.get<Settings>("/settings"), []);
  const [f, setF] = useState<Settings | null>(null);
  const [err, setErr] = useState<ApiError | null>(null);
  useEffect(() => { if (s.data) setF(s.data); }, [s.data]);
  if (!f) return <Loading />;
  const set = (k: keyof Settings) => (v: string) => setF({ ...f, [k]: v });
  const setRule = (k: keyof Settings["duplicateRules"]) => (v: string) => setF({ ...f, duplicateRules: { ...f.duplicateRules, [k]: Number(v) } });
  const save = async () => {
    try { setF(await api.put<Settings>("/settings", { ...f, defaultSlotMinutes: Number(f.defaultSlotMinutes), sessionIdleMinutes: Number(f.sessionIdleMinutes) })); setErr(null); toast("Settings saved"); }
    catch (e) { setErr(e as ApiError); }
  };
  return (
    <div className="panel"><div className="panel-body">
      {err && <Banner kind="error">{err.message}</Banner>}
      <fieldset className="fieldset"><legend>Clinic details (printed on prescriptions and invoices)</legend>
        <div className="grid g3">
          <TextField label="Clinic name" required value={f.clinicName} onChange={set("clinicName")} error={err?.fields.clinicName} className="span2" />
          <TextField label="Phone" value={f.phone} onChange={set("phone")} />
          <TextField label="Address" value={f.address} onChange={set("address")} className="span-all" />
          <TextField label="City" value={f.city} onChange={set("city")} />
          <TextField label="State" value={f.state} onChange={set("state")} />
          <TextField label="PIN code" value={f.pincode} onChange={set("pincode")} error={err?.fields.pincode} />
          <TextField label="Email" value={f.email} onChange={set("email")} error={err?.fields.email} />
          <TextField label="Clinical establishment registration" value={f.registrationNumber} onChange={set("registrationNumber")} />
          <TextField label="GSTIN" value={f.gstin} onChange={set("gstin")} />
          <TextField label="ABDM Health Facility Registry ID" value={f.hfrId} onChange={set("hfrId")} hint="Issued after HFR registration; needed for live ABDM" />
        </div>
      </fieldset>
      <fieldset className="fieldset"><legend>Numbering and sessions</legend>
        <div className="grid g4">
          <TextField label="Patient ID prefix" value={f.patientIdPrefix} onChange={(v) => set("patientIdPrefix")(v.toUpperCase())} hint={`e.g. ${f.patientIdPrefix}-2026-000123`} error={err?.fields.patientIdPrefix} />
          <TextField label="Invoice prefix" value={f.invoicePrefix} onChange={(v) => set("invoicePrefix")(v.toUpperCase())} error={err?.fields.invoicePrefix} />
          <TextField label="Walk-in slot length" value={f.defaultSlotMinutes} onChange={set("defaultSlotMinutes")} unit="min" inputMode="numeric" />
          <TextField label="Sign out after inactivity" value={f.sessionIdleMinutes} onChange={set("sessionIdleMinutes")} unit="min" inputMode="numeric" error={err?.fields.sessionIdleMinutes} />
        </div>
      </fieldset>
      <fieldset className="fieldset"><legend>Duplicate patient detection</legend>
        <p className="small muted">A new registration is flagged when matching records score at or above the threshold. Weights add up per matching field.</p>
        <div className="grid g4">
          <TextField label="Flag at score" value={f.duplicateRules.threshold} onChange={setRule("threshold")} inputMode="numeric" error={err?.fields["duplicateRules.threshold"]} />
          <TextField label="Mobile match weight" value={f.duplicateRules.weightMobile} onChange={setRule("weightMobile")} inputMode="numeric" />
          <TextField label="Date of birth weight" value={f.duplicateRules.weightDateOfBirth} onChange={setRule("weightDateOfBirth")} inputMode="numeric" />
          <TextField label="Similar name weight" value={f.duplicateRules.weightName} onChange={setRule("weightName")} inputMode="numeric" />
          <TextField label="Gender match weight" value={f.duplicateRules.weightGender} onChange={setRule("weightGender")} inputMode="numeric" />
          <TextField label="Name similarity needed" value={f.duplicateRules.nameSimilarityMin} onChange={(v) => setF({ ...f, duplicateRules: { ...f.duplicateRules, nameSimilarityMin: Number(v) } })} hint="0.1 (loose) to 1.0 (exact)" inputMode="decimal" />
        </div>
      </fieldset>
      <Button variant="primary" onClick={save}>Save settings</Button>
    </div></div>
  );
}

function DoctorsTab() {
  const toast = useToast();
  const doctors = useAsync(() => api.get<Doctor[]>("/doctors", { includeInactive: true }), []);
  const users = useAsync(() => api.get<UserRow[]>("/users"), []);
  const [editing, setEditing] = useState<Doctor | "new" | null>(null);
  if (doctors.loading) return <Loading />;
  const candidates = (users.data ?? []).filter((u) => u.roles.includes("DOCTOR") && !u.doctorId);
  return (
    <>
      <div className="row" style={{ justifyContent: "space-between", marginBottom: 12 }}>
        <p className="small muted" style={{ margin: 0 }}>A doctor profile links a user account (with the Doctor role) to registration details, fee and working hours.</p>
        <Button variant="primary" onClick={() => setEditing("new")} disabled={!candidates.length} title={candidates.length ? "" : "Create a user with the Doctor role first"}><IconPlus /> Add doctor profile</Button>
      </div>
      <div className="table-wrap">
        <table className="data">
          <thead><tr><th>Doctor</th><th>Registration</th><th>HPR ID</th><th className="right">Fee</th><th>Status</th><th /></tr></thead>
          <tbody>
            {doctors.data?.map((d) => (
              <tr key={d.id}>
                <td><div className="cell-main">{d.displayName}</div><div className="cell-sub">{[d.qualification, d.specialization].filter(Boolean).join(", ")}</div></td>
                <td className="small">{d.registrationNumber ?? "—"}{d.registrationCouncil && <div className="cell-sub">{d.registrationCouncil}</div>}</td>
                <td className="small">{d.hprId ?? <span className="muted">Not registered</span>}</td>
                <td className="num right">{rupees(d.consultationFeePaise, { decimals: false })}</td>
                <td>{d.isActive ? <span className="badge b-green">Active</span> : <span className="badge b-grey">Inactive</span>}</td>
                <td className="right"><Button size="sm" variant="ghost" onClick={() => setEditing(d)}>Edit</Button></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {editing && <DoctorModal doctor={editing === "new" ? null : editing} candidates={candidates} onClose={() => setEditing(null)} onSaved={() => { setEditing(null); doctors.reload(); users.reload(); toast("Doctor profile saved"); }} />}
    </>
  );
}

function DoctorModal({ doctor, candidates, onClose, onSaved }: { doctor: Doctor | null; candidates: UserRow[]; onClose: () => void; onSaved: () => void }) {
  const [f, setF] = useState({
    userId: doctor?.userId ?? candidates[0]?.id ?? "", displayName: doctor?.displayName ?? (candidates[0] ? `Dr. ${candidates[0].fullName}` : ""),
    qualification: doctor?.qualification ?? "", specialization: doctor?.specialization ?? "", registrationNumber: doctor?.registrationNumber ?? "",
    registrationCouncil: doctor?.registrationCouncil ?? "", hprId: doctor?.hprId ?? "", fee: ((doctor?.consultationFeePaise ?? 50000) / 100).toString(), isActive: doctor?.isActive ?? true,
  });
  const [err, setErr] = useState<ApiError | null>(null);
  const save = async () => {
    const body = { ...f, consultationFeePaise: toPaise(f.fee) ?? 0 };
    try { if (doctor) await api.put(`/doctors/${doctor.id}`, body); else await api.post("/doctors", body); onSaved(); } catch (e) { setErr(e as ApiError); }
  };
  return (
    <Modal title={doctor ? `Edit ${doctor.displayName}` : "Add doctor profile"} onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" onClick={save}>Save profile</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        {!doctor && <SelectField label="User account" value={f.userId} onChange={(v) => { const u = candidates.find((c) => c.id === v); setF({ ...f, userId: v, displayName: u ? `Dr. ${u.fullName}` : f.displayName }); }}
          options={candidates.map((c) => ({ value: c.id, label: `${c.fullName} (${c.username})` }))} className="span-all" />}
        <TextField label="Display name" required value={f.displayName} onChange={(v) => setF({ ...f, displayName: v })} error={err?.fields.displayName} className="span-all" />
        <TextField label="Qualification" value={f.qualification} onChange={(v) => setF({ ...f, qualification: v })} placeholder="e.g. MBBS, MD (Medicine)" />
        <TextField label="Specialization" value={f.specialization} onChange={(v) => setF({ ...f, specialization: v })} placeholder="e.g. General Medicine" />
        <TextField label="Registration number" value={f.registrationNumber} onChange={(v) => setF({ ...f, registrationNumber: v })} />
        <TextField label="Medical council" value={f.registrationCouncil} onChange={(v) => setF({ ...f, registrationCouncil: v })} placeholder="e.g. Karnataka Medical Council" />
        <TextField label="ABDM HPR ID" value={f.hprId} onChange={(v) => setF({ ...f, hprId: v })} hint="Healthcare Professional Registry" />
        <TextField label="Consultation fee (₹)" value={f.fee} onChange={(v) => setF({ ...f, fee: v })} inputMode="decimal" error={err?.fields.consultationFeePaise} />
        {doctor && <label className="check span-all"><input type="checkbox" checked={f.isActive} onChange={(e) => setF({ ...f, isActive: e.target.checked })} /> Accepting appointments</label>}
      </div>
    </Modal>
  );
}

function ServicesTab() {
  const toast = useToast();
  const list = useAsync(() => api.get<Service[]>("/billing/services", { includeInactive: true }), []);
  const [editing, setEditing] = useState<Service | "new" | null>(null);
  if (list.loading) return <Loading />;
  return (
    <>
      <div className="row" style={{ justifyContent: "flex-end", marginBottom: 12 }}><Button variant="primary" onClick={() => setEditing("new")}><IconPlus /> Add service</Button></div>
      <div className="table-wrap">
        <table className="data">
          <thead><tr><th>Code</th><th>Service</th><th>Category</th><th className="right">Price</th><th className="right">GST</th><th>Status</th><th /></tr></thead>
          <tbody>
            {list.data?.map((s) => (
              <tr key={s.id}>
                <td className="num">{s.code}</td><td className="cell-main">{s.name}</td><td>{humanize(s.category)}</td>
                <td className="num right">{rupees(s.pricePaise)}</td><td className="num right">{s.taxRateBp / 100}%</td>
                <td>{s.isActive ? <span className="badge b-green">Active</span> : <span className="badge b-grey">Inactive</span>}</td>
                <td className="right"><Button size="sm" variant="ghost" onClick={() => setEditing(s)}>Edit</Button></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {editing && <ServiceModal service={editing === "new" ? null : editing} onClose={() => setEditing(null)} onSaved={() => { setEditing(null); list.reload(); toast("Service saved"); }} />}
    </>
  );
}

function ServiceModal({ service, onClose, onSaved }: { service: Service | null; onClose: () => void; onSaved: () => void }) {
  const [f, setF] = useState({ code: service?.code ?? "", name: service?.name ?? "", category: service?.category ?? "PROCEDURE", price: ((service?.pricePaise ?? 0) / 100).toString(), tax: ((service?.taxRateBp ?? 0) / 100).toString(), isActive: service?.isActive ?? true });
  const [err, setErr] = useState<ApiError | null>(null);
  const save = async () => {
    const body = { code: f.code, name: f.name, category: f.category, pricePaise: toPaise(f.price), taxRateBp: Math.round(Number(f.tax) * 100), isActive: f.isActive };
    try { if (service) await api.put(`/billing/services/${service.id}`, body); else await api.post("/billing/services", body); onSaved(); } catch (e) { setErr(e as ApiError); }
  };
  return (
    <Modal title={service ? `Edit ${service.name}` : "Add service"} onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" onClick={save}>Save service</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        <TextField label="Code" value={f.code} onChange={(v) => setF({ ...f, code: v.toUpperCase() })} error={err?.fields.code} />
        <SelectField label="Category" value={f.category} onChange={(v) => setF({ ...f, category: v })} options={["CONSULTATION", "PROCEDURE", "INVESTIGATION", "OTHER"]} />
        <TextField label="Name" value={f.name} onChange={(v) => setF({ ...f, name: v })} error={err?.fields.name} className="span-all" />
        <TextField label="Price (₹)" value={f.price} onChange={(v) => setF({ ...f, price: v })} inputMode="decimal" error={err?.fields.pricePaise} />
        <TextField label="GST rate" value={f.tax} onChange={(v) => setF({ ...f, tax: v })} unit="%" inputMode="decimal" error={err?.fields.taxRateBp} />
        {service && <label className="check span-all"><input type="checkbox" checked={f.isActive} onChange={(e) => setF({ ...f, isActive: e.target.checked })} /> Available for billing</label>}
      </div>
    </Modal>
  );
}

function MedicinesTab() {
  const toast = useToast();
  const [q, setQ] = useState("");
  const list = useAsync(() => api.get<Medication[]>("/catalogue/medications", { q, includeInactive: true }), [q]);
  const [editing, setEditing] = useState<Medication | "new" | null>(null);
  return (
    <>
      <div className="row" style={{ justifyContent: "space-between", marginBottom: 12 }}>
        <input className="input" style={{ maxWidth: 320 }} placeholder="Search medicines" value={q} onChange={(e) => setQ(e.target.value)} aria-label="Search medicines" />
        <Button variant="primary" onClick={() => setEditing("new")}><IconPlus /> Add medicine</Button>
      </div>
      <Banner kind="info">Starter catalogue for demonstration. Review against the clinic's formulary and CDSCO-approved products before clinical use.</Banner>
      <div className="table-wrap">
        {list.loading && !list.data ? <Loading /> : (
          <table className="data">
            <thead><tr><th>Generic name</th><th>Brand</th><th>Strength</th><th>Form</th><th>Route</th><th>Schedule</th><th>Status</th><th /></tr></thead>
            <tbody>
              {list.data?.map((m) => (
                <tr key={m.id}>
                  <td className="cell-main">{m.genericName}</td><td>{m.brandName ?? "—"}</td><td>{m.strength}</td><td>{humanize(m.dosageForm)}</td><td>{humanize(m.defaultRoute)}</td>
                  <td>{m.drugSchedule ?? "—"}</td><td>{m.isActive ? <span className="badge b-green">Active</span> : <span className="badge b-grey">Inactive</span>}</td>
                  <td className="right"><Button size="sm" variant="ghost" onClick={() => setEditing(m)}>Edit</Button></td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      {editing && <MedicineModal med={editing === "new" ? null : editing} onClose={() => setEditing(null)} onSaved={() => { setEditing(null); list.reload(); toast("Medicine saved"); }} />}
    </>
  );
}

function MedicineModal({ med, onClose, onSaved }: { med: Medication | null; onClose: () => void; onSaved: () => void }) {
  const [f, setF] = useState({ genericName: med?.genericName ?? "", brandName: med?.brandName ?? "", strength: med?.strength ?? "", dosageForm: med?.dosageForm ?? "TABLET",
    defaultRoute: med?.defaultRoute ?? "ORAL", drugSchedule: med?.drugSchedule ?? "", isActive: med?.isActive ?? true });
  const [err, setErr] = useState<ApiError | null>(null);
  const save = async () => {
    try { if (med) await api.put(`/catalogue/medications/${med.id}`, f); else await api.post("/catalogue/medications", f); onSaved(); } catch (e) { setErr(e as ApiError); }
  };
  return (
    <Modal title={med ? `Edit ${med.genericName}` : "Add medicine"} onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" onClick={save}>Save medicine</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        <TextField label="Generic name" required value={f.genericName} onChange={(v) => setF({ ...f, genericName: v })} error={err?.fields.genericName} />
        <TextField label="Brand name" value={f.brandName} onChange={(v) => setF({ ...f, brandName: v })} />
        <TextField label="Strength" required value={f.strength} onChange={(v) => setF({ ...f, strength: v })} error={err?.fields.strength} placeholder="e.g. 500 mg" />
        <SelectField label="Dosage form" value={f.dosageForm} onChange={(v) => setF({ ...f, dosageForm: v })} options={["TABLET", "CAPSULE", "SYRUP", "SUSPENSION", "INJECTION", "INHALER", "CREAM", "OINTMENT", "EYE DROPS", "EAR DROPS", "POWDER", "GEL"]} />
        <SelectField label="Default route" value={f.defaultRoute} onChange={(v) => setF({ ...f, defaultRoute: v })} options={["ORAL", "TOPICAL", "INHALATION", "OPHTHALMIC", "OTIC", "NASAL", "SUBLINGUAL", "INTRAMUSCULAR", "INTRAVENOUS", "SUBCUTANEOUS"]} />
        <TextField label="Drug schedule" value={f.drugSchedule} onChange={(v) => setF({ ...f, drugSchedule: v })} placeholder="e.g. H, H1" />
        {med && <label className="check span-all"><input type="checkbox" checked={f.isActive} onChange={(e) => setF({ ...f, isActive: e.target.checked })} /> Available for prescribing</label>}
      </div>
    </Modal>
  );
}
