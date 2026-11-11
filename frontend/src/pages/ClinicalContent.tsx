// Clinical content review: decision-support knowledge and medicine safety data.
// Nothing is used for decision support until a clinical reviewer approves it.
import { useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import { humanize } from "../lib/format";
import { Banner, Button, ErrorBanner, Loading, Modal, PageHeader, Segmented, SelectField, Tabs, TextArea, TextField, useAsync, useToast } from "../components/ui";

type Status = "UNREVIEWED" | "APPROVED" | "RETIRED";
interface Reviewable { id: string; reviewStatus: Status; reviewedBy?: string | null; reviewNotes?: string | null; contentSource?: string }
interface Condition extends Reviewable {
  code: string; name: string; icd10: string; summary: string; references: { citation: string }[];
  features: { token: string; label: string; weight: number }[];
  content: { kind: string; text: string; generics: string[]; line: string; severities: string[] }[];
}
interface RedFlag extends Reviewable { code: string; title: string; criteria: { all?: string[]; any?: string[] }; guidance: string; reference?: string | null }
interface Med extends Reviewable {
  genericCode: string; genericName: string; brandName?: string | null; strength: string; dosageForm: string; route: string; category?: string | null;
  drugSchedule?: string | null; commonDosage?: string | null; warnings?: string | null; contraindicationRules: { tag: string; level: string; note?: string }[];
  contraindicationsText?: string | null; interactionsText?: string | null; highRisk: boolean; awareCategory?: string | null; restrictedAntimicrobial: boolean;
  defaultDose?: string | null; defaultFrequency?: string | null; defaultDurationDays?: number | null; reference?: string | null; isActive: boolean;
}
interface Interaction extends Reviewable { genericA: string; genericB: string; severity: string; description: string; reference: string }
interface Content { conditions: Condition[]; redFlags: RedFlag[]; medications: Med[]; interactions: Interaction[]; pendingReview: number; notice: string }

const STATUS_BADGE: Record<Status, string> = { UNREVIEWED: "b-amber", APPROVED: "b-green", RETIRED: "b-grey" };

function ReviewBar({ type, item, onDone }: { type: string; item: Reviewable; onDone: () => void }) {
  const { can } = useAuth();
  const toast = useToast();
  const [action, setAction] = useState<"APPROVE" | "RETIRE" | "REOPEN" | null>(null);
  const [notes, setNotes] = useState("");
  const [err, setErr] = useState<ApiError | null>(null);
  const [busy, setBusy] = useState(false);
  const submit = async () => {
    setBusy(true); setErr(null);
    try {
      await api.post(`/cds/content/${type}/${item.id}/review`, { action, notes: notes || null });
      toast(`${humanize(action!)} recorded`); setAction(null); setNotes(""); onDone();
    } catch (e) { setErr(e as ApiError); } finally { setBusy(false); }
  };
  return (
    <div className="row" style={{ justifyContent: "space-between" }}>
      <div className="small">
        <span className={`badge ${STATUS_BADGE[item.reviewStatus]}`}>{humanize(item.reviewStatus)}</span>
        {item.contentSource === "STARTER_DRAFT" && <span className="badge b-violet" style={{ marginLeft: 6 }}>AI-drafted starter content</span>}
        {item.reviewedBy && <span className="muted"> · {item.reviewedBy}{item.reviewNotes ? `: “${item.reviewNotes}”` : ""}</span>}
      </div>
      {can("cds.review") && (
        <div className="row">
          {item.reviewStatus !== "APPROVED" && <Button size="sm" onClick={() => setAction("APPROVE")}>Approve…</Button>}
          {item.reviewStatus !== "RETIRED" && <Button size="sm" variant="ghost" onClick={() => setAction("RETIRE")}>Retire</Button>}
          {item.reviewStatus !== "UNREVIEWED" && <Button size="sm" variant="ghost" onClick={() => setAction("REOPEN")}>Reopen</Button>}
        </div>
      )}
      {action && (
        <Modal title={`${humanize(action)} content item`} onClose={() => setAction(null)}
          footer={<><Button onClick={() => setAction(null)}>Cancel</Button><Button variant="primary" busy={busy} onClick={submit}>Confirm</Button></>}>
          {action === "APPROVE" && <Banner kind="warn">Approve only after checking the content and its citation against the current source. Approved content is shown to doctors as decision support.</Banner>}
          {err && <Banner kind="error">{err.message}</Banner>}
          <TextArea label={action === "APPROVE" ? "What did you check it against?" : "Notes"} required={action === "APPROVE"} value={notes} onChange={setNotes}
            rows={3} error={err?.fields.notes} hint="Recorded with your name in the item and the audit log" />
        </Modal>
      )}
    </div>
  );
}

export function ClinicalContentPage() {
  const { can } = useAuth();
  const [status, setStatus] = useState<"ALL" | Status>("UNREVIEWED");
  const [tab, setTab] = useState<"conditions" | "redFlags" | "medications" | "interactions">("conditions");
  const res = useAsync(() => api.get<Content>("/cds/content", { status }), [status]);
  const [editing, setEditing] = useState<Med | null>(null);
  const [addingProduct, setAddingProduct] = useState(false);
  const d = res.data;
  return (
    <>
      <PageHeader title="Clinical content" subtitle={d ? `${d.pendingReview} item(s) awaiting clinical review` : undefined} />
      {d && <Banner kind="warn">{d.notice}</Banner>}
      <div className="row" style={{ justifyContent: "space-between", marginBottom: 12 }}>
        <Tabs tabs={[{ id: "conditions" as const, label: `Conditions${d ? ` (${d.conditions.length})` : ""}` }, { id: "redFlags" as const, label: `Red flags${d ? ` (${d.redFlags.length})` : ""}` },
          { id: "medications" as const, label: `Medicines${d ? ` (${d.medications.length})` : ""}` }, { id: "interactions" as const, label: `Interactions${d ? ` (${d.interactions.length})` : ""}` }]}
          value={tab} onChange={setTab} />
        <Segmented label="Review status" value={status} onChange={setStatus}
          options={[{ id: "UNREVIEWED", label: "Awaiting review" }, { id: "APPROVED", label: "Approved" }, { id: "RETIRED", label: "Retired" }, { id: "ALL", label: "All" }]} />
      </div>
      {res.loading && !d ? <Loading /> : res.error ? <ErrorBanner error={res.error} /> : d && (
        <div className="panel"><div className="panel-body">
          {tab === "conditions" && (d.conditions.length ? d.conditions.map((c) => (
            <div key={c.id} className="content-item stack" style={{ gap: 6 }}>
              <div><strong>{c.name}</strong> <span className="chip tabular">{c.icd10}</span></div>
              <div className="small">{c.summary}</div>
              <details><summary className="small">Findings ({c.features.length}), questions and options ({c.content.length})</summary>
                <div className="small"><strong>Weighted findings:</strong> {c.features.map((f) => `${f.label} (${f.weight})`).join("; ")}</div>
                <ul className="small">{c.content.map((x, i) => <li key={i}><strong>{humanize(x.kind)}{x.line ? ` · ${humanize(x.line)}` : ""}:</strong> {x.text}{x.generics.length ? <span className="muted"> [{x.generics.join(", ")}]</span> : null}{x.severities.length ? <span className="muted"> ({x.severities.map(humanize).join("/")})</span> : null}</li>)}</ul>
              </details>
              {c.references.map((r) => <div key={r.citation} className="ref">{r.citation}</div>)}
              <ReviewBar type="condition" item={c} onDone={res.reload} />
            </div>
          )) : <p className="muted">No conditions with this status.</p>)}
          {tab === "redFlags" && (d.redFlags.length ? d.redFlags.map((f) => (
            <div key={f.id} className="content-item stack" style={{ gap: 6 }}>
              <strong>{f.title}</strong>
              <div className="small">When: {[f.criteria.all?.length ? `all of [${f.criteria.all.join("; ")}]` : "", f.criteria.any?.length ? `any of [${f.criteria.any.join("; ")}]` : ""].filter(Boolean).join(" and ")}</div>
              <div className="small">Guidance: {f.guidance}</div>
              {f.reference && <div className="ref">{f.reference}</div>}
              <ReviewBar type="red-flag" item={f} onDone={res.reload} />
            </div>
          )) : <p className="muted">No red flags with this status.</p>)}
          {tab === "medications" && can("catalogue.manage") && (
            <div className="row" style={{ justifyContent: "space-between", marginBottom: 8 }}>
              <span className="small muted">Generic products carry the reviewed clinical information; brands added by the clinic share the generic's code and start awaiting review.</span>
              <Button size="sm" onClick={() => setAddingProduct(true)}>Add brand / product</Button>
            </div>
          )}
          {tab === "medications" && (d.medications.length ? d.medications.map((m) => (
            <div key={m.id} className="content-item stack" style={{ gap: 4 }}>
              <div className="row" style={{ justifyContent: "space-between" }}>
                <div><strong>{m.brandName ? `${m.brandName} (${m.genericName})` : m.genericName}</strong> {m.strength} <span className="muted">{humanize(m.dosageForm)}</span>
                  {" "}<span className="chip tabular">{m.genericCode}</span> {m.brandName ? <span className="badge b-blue">Brand</span> : <span className="badge b-grey">Generic</span>}
                  {m.highRisk && <span className="badge b-red" style={{ marginLeft: 4 }}>High-alert</span>}
                  {m.awareCategory && <span className="badge b-amber" style={{ marginLeft: 4 }}>AWaRe {humanize(m.awareCategory)}</span>}</div>
                {can("catalogue.manage") && <Button size="sm" variant="ghost" onClick={() => setEditing(m)}>Edit clinical information</Button>}
              </div>
              <div className="small muted">{m.category}{m.drugSchedule ? ` · Schedule ${m.drugSchedule}` : ""}</div>
              {m.commonDosage && <div className="small"><strong>Common dosage:</strong> {m.commonDosage}</div>}
              {m.warnings && <div className="small"><strong>Warnings:</strong> {m.warnings}</div>}
              {m.contraindicationsText && <div className="small"><strong>Contraindications:</strong> {m.contraindicationsText}</div>}
              {m.contraindicationRules.length > 0 && <div className="small"><strong>Checked automatically:</strong> {m.contraindicationRules.map((r) => `${humanize(r.tag)} → ${humanize(r.level)}`).join("; ")}</div>}
              {m.interactionsText && <div className="small"><strong>Interactions:</strong> {m.interactionsText}</div>}
              {m.reference && <div className="ref">{m.reference}</div>}
              <ReviewBar type="medication" item={m} onDone={res.reload} />
            </div>
          )) : <p className="muted">No medicines with this status.</p>)}
          {tab === "interactions" && (d.interactions.length ? d.interactions.map((x) => (
            <div key={x.id} className="content-item stack" style={{ gap: 4 }}>
              <div><strong>{humanize(x.genericA)} + {humanize(x.genericB)}</strong> <span className={`badge ${x.severity === "CONTRAINDICATED" ? "b-red" : x.severity === "MAJOR" ? "b-amber" : "b-grey"}`}>{humanize(x.severity)}</span></div>
              <div className="small">{x.description}</div>
              <div className="ref">{x.reference}</div>
              <ReviewBar type="interaction" item={x} onDone={res.reload} />
            </div>
          )) : <p className="muted">No interactions with this status.</p>)}
        </div></div>
      )}
      {addingProduct && d && <AddProductModal generics={d.medications.filter((m) => !m.brandName)} onClose={() => setAddingProduct(false)} onSaved={() => { setAddingProduct(false); res.reload(); }} />}
      {editing && <MedEditModal med={editing} onClose={() => setEditing(null)} onSaved={() => { setEditing(null); res.reload(); }} />}
    </>
  );
}

function MedEditModal({ med, onClose, onSaved }: { med: Med; onClose: () => void; onSaved: () => void }) {
  const [m, setM] = useState(med);
  const [rules, setRules] = useState(JSON.stringify(med.contraindicationRules));
  const [err, setErr] = useState<ApiError | null>(null);
  const [busy, setBusy] = useState(false);
  const set = (p: Partial<Med>) => setM({ ...m, ...p });
  const save = async () => {
    setBusy(true); setErr(null);
    try {
      let parsed: unknown;
      try { parsed = JSON.parse(rules || "[]"); } catch { throw new ApiError(400, "VALIDATION_ERROR", "Contraindication rules must be valid JSON", { contraindicationRules: "Invalid JSON" }); }
      await api.put(`/catalogue/medications/${m.id}/clinical`, { category: m.category, commonDosage: m.commonDosage, warnings: m.warnings,
        contraindicationsText: m.contraindicationsText, interactionsText: m.interactionsText, reference: m.reference, defaultDose: m.defaultDose,
        defaultFrequency: m.defaultFrequency, defaultDurationDays: m.defaultDurationDays, contraindicationRules: parsed, highRisk: m.highRisk,
        awareCategory: m.awareCategory || null, restrictedAntimicrobial: m.restrictedAntimicrobial });
      onSaved();
    } catch (e) { setErr(e as ApiError); } finally { setBusy(false); }
  };
  return (
    <Modal wide title={`Clinical information: ${m.genericName} ${m.strength}`} onClose={onClose}
      footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" busy={busy} onClick={save}>Save (returns to review)</Button></>}>
      <Banner kind="info">Saving sends this medicine back to “awaiting review”; its information is not used for decision support until a clinical reviewer approves it again.</Banner>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        <TextField label="Category" value={m.category} onChange={(x) => set({ category: x })} />
        <SelectField label="WHO AWaRe category" value={m.awareCategory} onChange={(x) => set({ awareCategory: x })} options={["ACCESS", "WATCH", "RESERVE"]} placeholder="Not an antibiotic / not set" />
        <TextArea label="Common dosage" value={m.commonDosage} onChange={(x) => set({ commonDosage: x })} rows={2} />
        <TextArea label="Warnings" value={m.warnings} onChange={(x) => set({ warnings: x })} rows={2} />
        <TextArea label="Contraindications" value={m.contraindicationsText} onChange={(x) => set({ contraindicationsText: x })} rows={2} />
        <TextArea label="Interactions" value={m.interactionsText} onChange={(x) => set({ interactionsText: x })} rows={2} />
        <TextField label="Default dose (shown after approval)" value={m.defaultDose} onChange={(x) => set({ defaultDose: x })} />
        <TextField label="Default frequency" value={m.defaultFrequency} onChange={(x) => set({ defaultFrequency: x })} />
        <TextField label="Default duration (days)" value={m.defaultDurationDays ?? ""} onChange={(x) => set({ defaultDurationDays: x ? Number(x) : null })} inputMode="numeric" error={err?.fields.defaultDurationDays} />
        <TextField label="Reference (required before approval)" value={m.reference} onChange={(x) => set({ reference: x })} />
        <TextArea label="Automated contraindication rules (JSON)" className="span2" value={rules} onChange={setRules} rows={3} error={err?.fields.contraindicationRules}
          hint='e.g. [{"tag":"PREGNANCY","level":"CONTRAINDICATED","note":"..."}]; tags: PREGNANCY, BREASTFEEDING, AGE_UNDER_n' />
        <label className="small"><input type="checkbox" checked={m.highRisk} onChange={(e) => set({ highRisk: e.target.checked })} /> High-alert medicine</label>
        <label className="small"><input type="checkbox" checked={m.restrictedAntimicrobial} onChange={(e) => set({ restrictedAntimicrobial: e.target.checked })} /> Restricted under the clinic antimicrobial policy</label>
      </div>
    </Modal>
  );
}

function AddProductModal({ generics, onClose, onSaved }: { generics: Med[]; onClose: () => void; onSaved: () => void }) {
  const codes = Array.from(new Map(generics.map((g) => [g.genericCode, g])).values()).sort((a, b) => a.genericName.localeCompare(b.genericName));
  const [code, setCode] = useState("");
  const [v, setV] = useState({ brandName: "", strength: "", dosageForm: "", defaultRoute: "", drugSchedule: "" });
  const [err, setErr] = useState<ApiError | null>(null);
  const g = codes.find((x) => x.genericCode === code);
  const save = async () => {
    setErr(null);
    try {
      await api.post("/catalogue/medications", { genericName: g?.genericName, genericCode: code, brandName: v.brandName || null,
        strength: v.strength || g?.strength, dosageForm: v.dosageForm || g?.dosageForm, defaultRoute: v.defaultRoute || g?.route, drugSchedule: v.drugSchedule || g?.drugSchedule || null });
      onSaved();
    } catch (e) { setErr(e as ApiError); }
  };
  return (
    <Modal title="Add brand or product" onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" disabled={!code} onClick={save}>Add</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        <SelectField label="Generic medicine" required className="span2" value={code} onChange={setCode} placeholder="Select"
          options={codes.map((x) => ({ value: x.genericCode, label: `${x.genericName} (${x.genericCode})` }))} />
        <TextField label="Brand name" value={v.brandName} onChange={(x) => setV({ ...v, brandName: x })} hint="As registered; leave blank for another generic strength/form" />
        <TextField label="Strength" value={v.strength} onChange={(x) => setV({ ...v, strength: x })} placeholder={g?.strength} />
        <TextField label="Dosage form" value={v.dosageForm} onChange={(x) => setV({ ...v, dosageForm: x.toUpperCase() })} placeholder={g?.dosageForm} />
        <TextField label="Schedule" value={v.drugSchedule} onChange={(x) => setV({ ...v, drugSchedule: x })} placeholder={g?.drugSchedule ?? "e.g. H"} />
      </div>
    </Modal>
  );
}
