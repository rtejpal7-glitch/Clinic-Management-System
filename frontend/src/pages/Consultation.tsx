import { useEffect, useMemo, useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Allergy, Consultation, Diagnosis, InvestigationItem, InvestigationOrder, Medication, Patient, RxItem, TimelineEntry, Vitals } from "../lib/types";
import { ageSex, dateOnly, dateTime, humanize } from "../lib/format";
import { bmi, validateVitals, VITAL_RANGES } from "../lib/validation";
import { navigate } from "../lib/router";
import {
  Banner, Button, Combo, ErrorBanner, Loading, MockFlag, Modal, StatusBadge, Tabs, TextArea, TextField, useAsync, useToast,
} from "../components/ui";
import { IconAlert, IconLock, IconPrinter, IconTrash } from "../components/icons";

type Tab = "notes" | "rx" | "previous";
/** Sensible starting dose by dosage form; the doctor always reviews it. */
function defaultDose(form: string): string {
  const f = form.toUpperCase();
  if (f === "TABLET") return "1 tablet";
  if (f === "CAPSULE") return "1 capsule";
  if (f === "SYRUP" || f === "SUSPENSION") return "5 ml";
  if (f === "INHALER") return "1 puff";
  if (f === "CREAM" || f === "OINTMENT" || f === "GEL") return "Apply thin layer";
  if (f.includes("DROPS")) return "1 drop";
  if (f === "POWDER") return "1 sachet";
  return "";
}
const FREQUENCIES = ["1-0-0", "0-0-1", "1-0-1", "1-1-1", "0-1-0", "1-1-1-1", "SOS", "Once a week", "Stat"];
const ROUTES = ["ORAL", "TOPICAL", "INHALATION", "OPHTHALMIC", "OTIC", "NASAL", "SUBLINGUAL", "INTRAMUSCULAR", "INTRAVENOUS", "SUBCUTANEOUS", "RECTAL", "VAGINAL"];

export function ConsultationPage({ id }: { id: string }) {
  const { can } = useAuth();
  const toast = useToast();
  const res = useAsync(() => api.get<Consultation>(`/consultations/${id}`), [id]);
  const [c, setC] = useState<Consultation | null>(null);
  const [tab, setTab] = useState<Tab>("notes");
  const [dirty, setDirty] = useState(false);
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);
  const [apiErr, setApiErr] = useState<ApiError | null>(null);
  const [confirmFinal, setConfirmFinal] = useState(false);
  const [addendum, setAddendum] = useState(false);
  const [fhir, setFhir] = useState<unknown>(null);

  useEffect(() => { if (res.data) { setC(res.data); setDirty(false); } }, [res.data]);
  useEffect(() => {
    const h = (e: BeforeUnloadEvent) => { if (dirty) e.preventDefault(); };
    window.addEventListener("beforeunload", h);
    return () => window.removeEventListener("beforeunload", h);
  }, [dirty]);

  const patient = useAsync(() => (res.data ? api.get<Patient>(`/patients/${res.data.patientId}`) : Promise.resolve(null)), [res.data?.patientId]);
  const allergies = useAsync(() => (res.data ? api.get<Allergy[]>(`/patients/${res.data.patientId}/allergies`) : Promise.resolve([])), [res.data?.patientId]);

  if (res.loading && !c) return <Loading />;
  if (res.error) return <ErrorBanner error={res.error} />;
  if (!c) return null;
  const editable = c.canEdit;
  const activeAllergies = (allergies.data ?? []).filter((a) => a.status === "ACTIVE");

  const update = (patch: Partial<Consultation>) => { setC({ ...c, ...patch }); setDirty(true); };
  const updateRx = (patch: Partial<Consultation["prescription"]>) => { setC({ ...c, prescription: { ...c.prescription, ...patch } }); setDirty(true); };

  const save = async (quiet = false): Promise<Consultation | null> => {
    const vErr = c.vitals ? validateVitals(c.vitals as Record<string, number | null>) : {};
    setErrors(vErr);
    if (Object.keys(vErr).length) { setTab("notes"); return null; }
    setBusy(true); setApiErr(null);
    try {
      const notes = await api.put<Consultation>(`/consultations/${c.id}`, {
        version: c.version, chiefComplaint: c.chiefComplaint, historyPresentIllness: c.historyPresentIllness, pastMedicalHistory: c.pastMedicalHistory,
        currentMedications: c.currentMedications, examinationNotes: c.examinationNotes, treatmentPlan: c.treatmentPlan,
        followUpInstructions: c.followUpInstructions, followUpDate: c.followUpDate || null, vitals: c.vitals ?? null, diagnoses: c.diagnoses,
      });
      const saved = await api.put<Consultation>(`/consultations/${c.id}/prescription`, {
        advice: c.prescription.advice, items: c.prescription.items, investigations: c.prescription.investigations,
      });
      setC({ ...saved, version: Math.max(saved.version, notes.version) });
      setDirty(false);
      if (!quiet) toast("Draft saved");
      return saved;
    } catch (e) {
      const err = e as ApiError;
      setApiErr(err);
      setErrors(err.fields);
      if (Object.keys(err.fields).some((k) => k.startsWith("items") || k.startsWith("investigations"))) setTab("rx");
      return null;
    } finally { setBusy(false); }
  };

  const finalize = async () => {
    const saved = await save(true);
    if (!saved) { setConfirmFinal(false); return; }
    try {
      const f = await api.post<Consultation>(`/consultations/${c.id}/finalize`);
      setC(f); setConfirmFinal(false);
      toast("Consultation finalized and locked");
    } catch (e) {
      const err = e as ApiError;
      setApiErr(err); setErrors(err.fields); setConfirmFinal(false); setTab("notes");
    }
  };

  const share = async () => {
    try {
      const r = await api.post<{ transaction: { status: string; errorMessage?: string }; resourceCount: number }>(`/consultations/${c.id}/abdm-share`);
      if (r.transaction.status === "SUCCESS") toast(`Simulated share: ${r.resourceCount} FHIR resources validated (mock, not transmitted)`);
      else toast(`Share ${humanize(r.transaction.status).toLowerCase()}: ${r.transaction.errorMessage ?? ""}`, "error");
    } catch (e) { toast((e as ApiError).message, "error"); }
  };

  return (
    <>
      <div className="patient-strip">
        <div>
          <div className="name">{c.patientName}</div>
          <div className="small muted tabular">{c.patientNumber} &nbsp; {patient.data ? ageSex(patient.data.ageYears, patient.data.gender) : ""} &nbsp; {patient.data?.bloodGroup ?? ""}</div>
        </div>
        <div className="small">
          <div>{c.doctorName}</div>
          <div className="muted">Started {dateTime(c.startedAt)}</div>
        </div>
        <div><StatusBadge status={c.status} /></div>
        <div style={{ flex: 1 }}>
          {activeAllergies.length > 0 ? (
            <span className="allergy-alert"><IconAlert /> Allergies: {activeAllergies.map((a) => a.substance).join(", ")}</span>
          ) : allergies.data ? <span className="small muted">No known allergies recorded</span> : null}
        </div>
        <div className="row no-print">
          {editable && <span className="small muted" aria-live="polite">{dirty ? "Unsaved changes" : "All changes saved"}</span>}
          {editable && <Button busy={busy} onClick={() => save()}>Save draft</Button>}
          {editable && can("clinical.finalize") && <Button variant="primary" onClick={() => setConfirmFinal(true)}>Finalize</Button>}
        </div>
      </div>

      {apiErr && <Banner kind="error">{apiErr.message}{apiErr.code === "STALE_VERSION" && <> <a href="#" onClick={(e) => { e.preventDefault(); res.reload(); }}>Reload</a></>}</Banner>}
      {c.status === "FINALIZED" && (
        <div className="panel" style={{ marginBottom: 16 }}>
          <div className="panel-body row" style={{ justifyContent: "space-between" }}>
            <span className="locked-note"><IconLock /> Finalized {dateTime(c.finalizedAt)}. This record is locked; corrections are added as addenda.</span>
            <div className="row">
              <Button onClick={() => navigate(`/consultations/${c.id}/print`)}><IconPrinter /> Prescription</Button>
              <Button onClick={() => navigate(`/consultations/${c.id}/print?doc=summary`)}><IconPrinter /> Visit summary</Button>
              {can("clinical.finalize") && <Button onClick={() => setAddendum(true)}>Add addendum</Button>}
              {can("fhir.export") && <Button onClick={async () => setFhir(await api.get(`/fhir/Bundle/${c.id}`))}>View FHIR bundle</Button>}
              {can("fhir.export") && <Button onClick={share}>Share via ABDM <MockFlag>Mock</MockFlag></Button>}
            </div>
          </div>
        </div>
      )}
      {!editable && c.status === "DRAFT" && <Banner kind="info">This draft belongs to {c.doctorName}. You can read it but only the treating doctor can edit.</Banner>}

      <Tabs tabs={[{ id: "notes", label: "Notes, vitals & diagnosis" }, { id: "rx", label: `Prescription & orders (${c.prescription.items.length + c.prescription.investigations.length})` }, { id: "previous", label: "Previous visits" }]}
        value={tab} onChange={setTab} />

      {tab === "notes" && <NotesTab c={c} editable={editable} errors={errors} update={update} />}
      {tab === "rx" && <RxTab c={c} editable={editable} errors={errors} updateRx={updateRx} allergies={activeAllergies} />}
      {tab === "previous" && <PreviousVisits patientId={c.patientId} currentId={c.id} />}

      {c.addenda.length > 0 && (
        <section style={{ marginTop: 20 }}>
          <div className="section-title"><h2>Addenda</h2></div>
          <div className="stack">
            {c.addenda.map((a) => (
              <div key={a.id} className="panel"><div className="panel-body">
                <div className="small muted">{dateTime(a.createdAt)} by {a.authorName}. Reason: {a.reason}</div>
                <p style={{ margin: "6px 0 0", whiteSpace: "pre-wrap" }}>{a.content}</p>
              </div></div>
            ))}
          </div>
        </section>
      )}

      {confirmFinal && (
        <Modal title="Finalize consultation?" onClose={() => setConfirmFinal(false)}
          footer={<><Button onClick={() => setConfirmFinal(false)}>Keep editing</Button><Button variant="primary" busy={busy} onClick={finalize}>Finalize and lock</Button></>}>
          <p>Finalizing locks the notes, diagnosis and prescription. Later corrections can only be added as addenda. The appointment will be marked completed.</p>
          <ul className="small">
            <li>Chief complaint: {c.chiefComplaint ? "recorded" : <strong style={{ color: "var(--red)" }}>missing</strong>}</li>
            <li>Diagnoses: {c.diagnoses.length || <strong style={{ color: "var(--red)" }}>none</strong>}</li>
            <li>Medicines: {c.prescription.items.length}, investigations: {c.prescription.investigations.length}</li>
          </ul>
        </Modal>
      )}
      {addendum && <AddendumModal id={c.id} onClose={() => setAddendum(false)} onSaved={(nc) => { setC(nc); setAddendum(false); }} />}
      {fhir !== null && (
        <Modal title="FHIR R4 document bundle" wide onClose={() => setFhir(null)}>
          <Banner kind="info">Generated from this consultation, modelled on the ABDM OPConsultRecord structure. It passes ClinicFlow's local structural checks; it has not been validated with the official NRCeS implementation guide.</Banner>
          <pre className="json">{JSON.stringify(fhir, null, 2)}</pre>
        </Modal>
      )}
    </>
  );
}

function NotesTab({ c, editable, errors, update }: { c: Consultation; editable: boolean; errors: Record<string, string>; update: (p: Partial<Consultation>) => void }) {
  const v: Vitals = c.vitals ?? {};
  const setV = (k: keyof Vitals, raw: string) => {
    const n = raw === "" ? null : Number(raw);
    update({ vitals: { ...v, [k]: n !== null && Number.isNaN(n) ? v[k] : n } });
  };
  const computedBmi = bmi(v.heightCm, v.weightKg);
  const units: Record<string, string> = { bpSystolic: "mmHg", bpDiastolic: "mmHg", pulse: "/min", temperatureC: "°C", respiratoryRate: "/min", spo2: "%", heightCm: "cm", weightKg: "kg" };
  return (
    <div className="split">
      <div className="stack">
        <div className="panel"><div className="panel-body stack">
          <TextArea label="Chief complaint" required value={c.chiefComplaint} onChange={(x) => update({ chiefComplaint: x })} rows={2} disabled={!editable} error={errors.chiefComplaint} placeholder="e.g. Fever and cough for 3 days" />
          <TextArea label="History of present illness" value={c.historyPresentIllness} onChange={(x) => update({ historyPresentIllness: x })} rows={3} disabled={!editable} />
          <div className="grid g2">
            <TextArea label="Past medical history" value={c.pastMedicalHistory} onChange={(x) => update({ pastMedicalHistory: x })} disabled={!editable} hint="Carried forward from the last visit" />
            <TextArea label="Current medications" value={c.currentMedications} onChange={(x) => update({ currentMedications: x })} disabled={!editable} />
          </div>
          <TextArea label="Examination findings" value={c.examinationNotes} onChange={(x) => update({ examinationNotes: x })} rows={3} disabled={!editable} />
        </div></div>
        <div className="panel">
          <div className="panel-head"><h3>Diagnosis</h3></div>
          <div className="panel-body stack">
            {errors.diagnoses && <span className="err small" style={{ color: "var(--red)", fontWeight: 600 }}>{errors.diagnoses}</span>}
            <DiagnosisEditor value={c.diagnoses} onChange={(d) => update({ diagnoses: d })} editable={editable} />
          </div>
        </div>
        <div className="panel"><div className="panel-body stack">
          <TextArea label="Treatment plan" value={c.treatmentPlan} onChange={(x) => update({ treatmentPlan: x })} disabled={!editable} />
          <div className="grid g3">
            <TextArea label="Follow-up instructions" value={c.followUpInstructions} onChange={(x) => update({ followUpInstructions: x })} className="span2" rows={2} disabled={!editable} />
            <TextField label="Review on" type="date" value={c.followUpDate} onChange={(x) => update({ followUpDate: x })} disabled={!editable} error={errors.followUpDate} />
          </div>
        </div></div>
      </div>
      <div className="panel">
        <div className="panel-head"><h3>Vital signs</h3>{v.recordedAt && <span className="small muted">{dateTime(v.recordedAt)}</span>}</div>
        <div className="panel-body">
          <div className="vitals-grid" style={{ gridTemplateColumns: "repeat(2, minmax(0,1fr))" }}>
            {(Object.keys(VITAL_RANGES) as (keyof Vitals)[]).map((k) => (
              <TextField key={k} label={VITAL_RANGES[k][2]} value={v[k] ?? ""} onChange={(x) => setV(k, x)} unit={units[k]} inputMode="decimal"
                disabled={!editable} error={errors[`vitals.${k}`]} />
            ))}
          </div>
          <p className="small" style={{ marginTop: 12, marginBottom: 0 }}>
            BMI <strong className="tabular">{computedBmi ?? "—"}</strong>{computedBmi ? <span className="muted"> kg/m² ({computedBmi < 18.5 ? "underweight" : computedBmi < 23 ? "normal for Asian adults" : computedBmi < 25 ? "overweight" : "obese"})</span> : null}
          </p>
        </div>
      </div>
    </div>
  );
}

function DiagnosisEditor({ value, onChange, editable }: { value: Diagnosis[]; onChange: (d: Diagnosis[]) => void; editable: boolean }) {
  const [free, setFree] = useState("");
  return (
    <>
      {value.length === 0 && <p className="small muted" style={{ margin: 0 }}>No diagnosis yet. At least one is needed to finalize.</p>}
      {value.map((d, i) => (
        <div key={i} className="row" style={{ justifyContent: "space-between", borderBottom: "1px solid var(--rule)", paddingBottom: 8 }}>
          <div>
            <strong>{d.display}</strong> {d.code && <span className="chip tabular">{d.codeSystem} {d.code}</span>}
          </div>
          <div className="row">
            <select className="select" style={{ width: 140 }} value={d.diagnosisType ?? (i === 0 ? "PRIMARY" : "SECONDARY")} disabled={!editable} aria-label="Diagnosis type"
              onChange={(e) => onChange(value.map((x, j) => (j === i ? { ...x, diagnosisType: e.target.value } : x)))}>
              {["PRIMARY", "SECONDARY", "PROVISIONAL"].map((t) => <option key={t} value={t}>{humanize(t)}</option>)}
            </select>
            {editable && <button className="icon-btn" aria-label={`Remove ${d.display}`} onClick={() => onChange(value.filter((_, j) => j !== i))}><IconTrash /></button>}
          </div>
        </div>
      ))}
      {editable && (
        <div className="grid g2">
          <Combo<{ codeSystem: string; code: string; display: string }> label="Add coded diagnosis (ICD-10)" placeholder="Search e.g. hypertension, J06"
            search={(q) => api.get("/catalogue/diagnoses", { q })} render={(d) => <><span className="tabular chip">{d.code}</span> {d.display}</>}
            onPick={(d) => onChange([...value, { codeSystem: d.codeSystem, code: d.code, display: d.display, diagnosisType: value.length ? "SECONDARY" : "PRIMARY" }])} />
          <div className="field">
            <label htmlFor="free-dx">Or free-text diagnosis</label>
            <div className="row" style={{ flexWrap: "nowrap" }}>
              <input id="free-dx" className="input" value={free} onChange={(e) => setFree(e.target.value)} placeholder="Uncoded diagnosis"
                onKeyDown={(e) => { if (e.key === "Enter" && free.trim().length > 1) { e.preventDefault(); onChange([...value, { codeSystem: "FREE_TEXT", display: free.trim(), diagnosisType: value.length ? "SECONDARY" : "PRIMARY" }]); setFree(""); } }} />
              <Button disabled={free.trim().length < 2} onClick={() => { onChange([...value, { codeSystem: "FREE_TEXT", display: free.trim(), diagnosisType: value.length ? "SECONDARY" : "PRIMARY" }]); setFree(""); }}>Add</Button>
            </div>
          </div>
        </div>
      )}
    </>
  );
}

function RxTab({ c, editable, errors, updateRx, allergies }: {
  c: Consultation; editable: boolean; errors: Record<string, string>; updateRx: (p: Partial<Consultation["prescription"]>) => void; allergies: Allergy[];
}) {
  const items = c.prescription.items;
  const setItem = (i: number, patch: Partial<RxItem>) => updateRx({ items: items.map((it, j) => (j === i ? { ...it, ...patch } : it)) });
  const orders = c.prescription.investigations;
  const allergyHit = useMemo(() => {
    const names = allergies.filter((a) => a.category === "MEDICATION").map((a) => a.substance.toLowerCase());
    return items.filter((it) => names.some((n) => it.medicineName.toLowerCase().includes(n) || n.includes(it.medicineName.toLowerCase())));
  }, [items, allergies]);
  return (
    <div className="stack">
      <Banner kind="info">ClinicFlow does not perform automated drug-interaction, dose or allergy checking. Review the patient's allergies and current medications before prescribing.</Banner>
      {allergyHit.length > 0 && <Banner kind="warn">Name match with a recorded allergy: {allergyHit.map((i) => i.medicineName).join(", ")}. This is a simple text match, not a clinical check.</Banner>}
      <div className="panel">
        <div className="panel-head"><h3><span style={{ fontFamily: "Georgia, serif", color: "var(--blue)", fontSize: "1.2rem" }}>℞</span> Medicines</h3></div>
        <div className="panel-body">
          {items.length > 0 && (
            <div className="rx-row rx-head" aria-hidden="true"><span>Medicine</span><span>Dose</span><span>Route</span><span>Frequency</span><span>Days</span><span>Qty</span><span>Instructions</span><span /></div>
          )}
          {items.map((it, i) => (
            <div className="rx-row" key={i}>
              <div><strong>{it.medicineName}</strong><div className="cell-sub">{[it.strength, it.dosageForm && humanize(it.dosageForm)].filter(Boolean).join(", ")}</div></div>
              <RxInput label="Dose" value={it.dose} onChange={(v) => setItem(i, { dose: v })} disabled={!editable} error={errors[`items[${i}].dose`]} />
              <select className="select" aria-label="Route" value={it.route} disabled={!editable} onChange={(e) => setItem(i, { route: e.target.value })}>{ROUTES.map((r) => <option key={r} value={r}>{humanize(r)}</option>)}</select>
              <div>
                <input className="input" aria-label="Frequency" list="freq" value={it.frequency} disabled={!editable} onChange={(e) => setItem(i, { frequency: e.target.value })} aria-invalid={!!errors[`items[${i}].frequency`]} />
              </div>
              <RxInput label="Days" value={it.durationDays ?? ""} onChange={(v) => setItem(i, { durationDays: v ? Number(v.replace(/\D/g, "")) : null })} disabled={!editable} error={errors[`items[${i}].durationDays`]} />
              <RxInput label="Quantity" value={it.quantity ?? ""} onChange={(v) => setItem(i, { quantity: v ? Number(v) : null })} disabled={!editable} />
              <RxInput label="Instructions" value={it.instructions ?? ""} onChange={(v) => setItem(i, { instructions: v })} disabled={!editable} placeholder="e.g. After food" />
              {editable ? <button className="icon-btn" aria-label={`Remove ${it.medicineName}`} onClick={() => updateRx({ items: items.filter((_, j) => j !== i) })}><IconTrash /></button> : <span />}
            </div>
          ))}
          <datalist id="freq">{FREQUENCIES.map((f) => <option key={f} value={f} />)}</datalist>
          {items.length === 0 && <p className="small muted">No medicines prescribed.</p>}
          {editable && (
            <div style={{ marginTop: 12, maxWidth: 480 }}>
              <Combo<Medication> label="Add medicine" placeholder="Search the medicine catalogue" search={(q) => api.get("/catalogue/medications", { q })}
                render={(m) => <><strong>{m.genericName}</strong> {m.strength} <span className="muted">{humanize(m.dosageForm)}</span></>}
                onPick={(m) => updateRx({ items: [...items, { medicationId: m.id, medicineName: m.genericName, strength: m.strength, dosageForm: m.dosageForm,
                  route: m.defaultRoute, dose: defaultDose(m.dosageForm), frequency: "1-0-1", durationDays: 5 }] })} />
            </div>
          )}
        </div>
      </div>
      <div className="panel">
        <div className="panel-head"><h3>Investigation orders</h3></div>
        <div className="panel-body">
          {orders.length === 0 && <p className="small muted">No investigations ordered.</p>}
          {orders.map((o, i) => (
            <div key={i} className="row" style={{ justifyContent: "space-between", borderBottom: "1px solid var(--rule)", padding: "8px 0" }}>
              <div><strong>{o.name}</strong> <span className="muted small">{humanize(o.category)}{o.loincCode ? `, LOINC ${o.loincCode}` : ""}</span></div>
              <div className="row">
                <select className="select" style={{ width: 120 }} value={o.priority ?? "ROUTINE"} disabled={!editable} aria-label="Priority"
                  onChange={(e) => updateRx({ investigations: orders.map((x, j) => (j === i ? { ...x, priority: e.target.value } : x)) })}>
                  <option value="ROUTINE">Routine</option><option value="URGENT">Urgent</option>
                </select>
                {o.status && o.status !== "ORDERED" && <StatusBadge status={o.status} />}
                {editable && <button className="icon-btn" aria-label={`Remove ${o.name}`} onClick={() => updateRx({ investigations: orders.filter((_, j) => j !== i) })}><IconTrash /></button>}
              </div>
            </div>
          ))}
          {editable && (
            <div className="grid g2" style={{ marginTop: 12 }}>
              <Combo<InvestigationItem> label="Order from catalogue" placeholder="e.g. CBC, lipid, X-ray" search={(q) => api.get("/catalogue/investigations", { q })}
                render={(t) => <><strong>{t.name}</strong> <span className="muted">{humanize(t.category)}</span></>}
                onPick={(t) => updateRx({ investigations: [...orders, { investigationId: t.id, name: t.name, category: t.category, loincCode: t.loincCode, priority: "ROUTINE" } as InvestigationOrder] })} />
              <OtherOrder onAdd={(o) => updateRx({ investigations: [...orders, o] })} />
            </div>
          )}
        </div>
      </div>
      <div className="panel"><div className="panel-body">
        <TextArea label="Advice (non-medication instructions)" value={c.prescription.advice} onChange={(v) => updateRx({ advice: v })} disabled={!editable}
          placeholder="Diet, rest, fluids, warning signs to return early" rows={3} />
      </div></div>
    </div>
  );
}

function RxInput({ label, value, onChange, disabled, error, placeholder }: { label: string; value: string | number; onChange: (v: string) => void; disabled?: boolean; error?: string; placeholder?: string }) {
  return <input className="input" aria-label={label} title={error} value={value} onChange={(e) => onChange(e.target.value)} disabled={disabled} aria-invalid={!!error} placeholder={disabled ? "" : placeholder ?? label} />;
}

function OtherOrder({ onAdd }: { onAdd: (o: InvestigationOrder) => void }) {
  const [name, setName] = useState("");
  const [cat, setCat] = useState("LAB");
  return (
    <div className="field">
      <label htmlFor="other-order">Other investigation</label>
      <div className="row" style={{ flexWrap: "nowrap" }}>
        <input id="other-order" className="input" value={name} onChange={(e) => setName(e.target.value)} placeholder="Not in catalogue" />
        <select className="select" style={{ width: 120 }} value={cat} onChange={(e) => setCat(e.target.value)} aria-label="Category"><option value="LAB">Lab</option><option value="RADIOLOGY">Radiology</option><option value="OTHER">Other</option></select>
        <Button disabled={name.trim().length < 2} onClick={() => { onAdd({ name: name.trim(), category: cat, priority: "ROUTINE" }); setName(""); }}>Add</Button>
      </div>
    </div>
  );
}

function PreviousVisits({ patientId, currentId }: { patientId: string; currentId: string }) {
  const tl = useAsync(() => api.get<TimelineEntry[]>(`/patients/${patientId}/timeline`), [patientId]);
  if (tl.loading) return <Loading />;
  const prev = (tl.data ?? []).filter((e) => e.consultationId !== currentId);
  if (!prev.length) return <p className="muted">This is the patient's first recorded consultation.</p>;
  return (
    <div className="panel"><div className="panel-body">
      <ul className="timeline">
        {prev.map((e) => (
          <li key={e.consultationId}>
            <div><div className="tl-date">{dateOnly(e.startedAt)}</div><div className="small muted">{e.doctorName}</div></div>
            <div className="stack" style={{ gap: 6 }}>
              <div className="row" style={{ justifyContent: "space-between" }}><strong>{e.chiefComplaint ?? "—"}</strong><a className="small" href={`#/consultations/${e.consultationId}`}>Open</a></div>
              {e.diagnoses.length > 0 && <div className="chips">{e.diagnoses.map((d) => <span key={d} className="chip">{d}</span>)}</div>}
              {e.medicines.length > 0 && <div className="small">{e.medicines.join("; ")}</div>}
            </div>
          </li>
        ))}
      </ul>
    </div></div>
  );
}

function AddendumModal({ id, onClose, onSaved }: { id: string; onClose: () => void; onSaved: (c: Consultation) => void }) {
  const toast = useToast();
  const [reason, setReason] = useState("");
  const [content, setContent] = useState("");
  const [err, setErr] = useState<ApiError | null>(null);
  const save = async () => {
    try { const c = await api.post<Consultation>(`/consultations/${id}/addenda`, { reason, content }); toast("Addendum added"); onSaved(c); }
    catch (e) { setErr(e as ApiError); }
  };
  return (
    <Modal title="Add addendum" onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" disabled={reason.trim().length < 3 || content.trim().length < 3} onClick={save}>Add addendum</Button></>}>
      <p className="small muted">The original record stays unchanged. The addendum is time-stamped and attributed to you.</p>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="stack">
        <TextField label="Reason" required value={reason} onChange={setReason} placeholder="e.g. Lab result received, correction to dose" error={err?.fields.reason} />
        <TextArea label="Addendum" required value={content} onChange={setContent} rows={4} error={err?.fields.content} />
      </div>
    </Modal>
  );
}
