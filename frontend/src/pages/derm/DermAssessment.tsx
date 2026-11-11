// Structured dermatology assessment: short coded selections (chips) plus a few
// free-text notes. Designed to be filled in quickly during the visit.
import { useEffect, useState } from "react";
import { api, ApiError } from "../../lib/api";
import { DermAssessment, DermVocabulary, VocabItem, dermVocabulary, emptyAssessment } from "../../lib/derm";
import { humanize } from "../../lib/format";
import { Banner, Button, ErrorBanner, Loading, SelectField, TextArea, TextField, useAsync, useToast } from "../../components/ui";
import { PhotosPanel } from "./Photos";

function Chips({ label, items, value, onChange, disabled, single, hint }: {
  label: string; items: VocabItem[]; value: string[]; onChange: (v: string[]) => void; disabled?: boolean; single?: boolean; hint?: string;
}) {
  const toggle = (code: string) => {
    if (disabled) return;
    if (single) onChange(value.includes(code) ? [] : [code]);
    else onChange(value.includes(code) ? value.filter((c) => c !== code) : [...value, code]);
  };
  return (
    <fieldset className="chip-field">
      <legend>{label}{hint && <span className="hint"> {hint}</span>}</legend>
      <div className="chip-set" role="group" aria-label={label}>
        {items.map((i) => (
          <button type="button" key={i.code} className="chip-toggle" aria-pressed={value.includes(i.code)} disabled={disabled && !value.includes(i.code)}
            onClick={() => toggle(i.code)}>{i.label}</button>
        ))}
      </div>
    </fieldset>
  );
}

function TriState({ label, value, onChange, disabled }: { label: string; value?: boolean | null; onChange: (v: boolean | null) => void; disabled?: boolean }) {
  const opts: [string, boolean | null][] = [["Yes", true], ["No", false], ["Not asked", null]];
  return (
    <div className="field">
      <label>{label}</label>
      <div className="segmented" role="group" aria-label={label}>
        {opts.map(([l, v]) => <button type="button" key={l} aria-pressed={value === v || (v === null && value == null)} disabled={disabled} onClick={() => onChange(v)}>{l}</button>)}
      </div>
    </div>
  );
}

export function DermAssessmentTab({ consultationId, editable, patientSex, onSaved }: {
  consultationId: string; editable: boolean; patientSex?: string; onSaved?: () => void;
}) {
  const toast = useToast();
  const vocab = useAsync<DermVocabulary>(() => dermVocabulary(), []);
  const res = useAsync(() => api.get<DermAssessment>(`/consultations/${consultationId}/derm`), [consultationId]);
  const [a, setA] = useState<DermAssessment>(emptyAssessment());
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<ApiError | null>(null);

  useEffect(() => { if (res.data) { setA({ ...emptyAssessment(), ...res.data }); setDirty(false); } }, [res.data]);

  if ((res.loading && !res.data) || (vocab.loading && !vocab.data)) return <Loading label="Loading dermatology assessment" />;
  if (res.error) return <ErrorBanner error={res.error} />;
  if (vocab.error || !vocab.data) return <ErrorBanner error={vocab.error} />;
  const v = vocab.data;
  const canEdit = editable && res.data?.canEdit !== false;
  const set = (patch: Partial<DermAssessment>) => { setA({ ...a, ...patch }); setDirty(true); };
  const f = err?.fields ?? {};

  const save = async () => {
    setBusy(true); setErr(null);
    try {
      const saved = await api.put<DermAssessment>(`/consultations/${consultationId}/derm`, a);
      setA({ ...emptyAssessment(), ...saved }); setDirty(false);
      toast("Dermatology assessment saved");
      onSaved?.();
    } catch (e) { setErr(e as ApiError); } finally { setBusy(false); }
  };
  const vocabOpts = (items: VocabItem[]) => items.map((i) => ({ value: i.code, label: i.label }));
  const pregnancyOpts = v.pregnancyStatuses.filter((p) => patientSex !== "MALE" || p === "NOT_APPLICABLE" || p === "UNKNOWN");

  return (
    <div className="stack">
      {err && <Banner kind="error">{err.message}</Banner>}
      {!res.data?.exists && canEdit && <Banner kind="info">Record the findings below. Decision support uses only these coded findings; nothing is inferred from free text except a history of atopy.</Banner>}
      <div className="derm-grid">
        <section className="panel">
          <div className="panel-head"><h3>Symptoms &amp; history</h3></div>
          <div className="panel-body stack">
            <div className="grid g3">
              <SelectField label="Type of skin problem" value={a.problemType} onChange={(x) => set({ problemType: x || null })} options={vocabOpts(v.problemTypes)}
                placeholder="Select" disabled={!canEdit} error={f.problemType} required />
              <div className="field">
                <label htmlFor="dur">Duration</label>
                <div className="row" style={{ flexWrap: "nowrap" }}>
                  <input id="dur" className="input" style={{ width: 80 }} inputMode="numeric" value={a.durationValue ?? ""} disabled={!canEdit}
                    onChange={(e) => set({ durationValue: e.target.value === "" ? null : Number(e.target.value.replace(/\D/g, "")) })} aria-invalid={!!f.durationValue} />
                  <select className="select" aria-label="Duration unit" value={a.durationUnit ?? ""} disabled={!canEdit} onChange={(e) => set({ durationUnit: e.target.value || null })}>
                    <option value="">unit</option>{v.durationUnits.map((u) => <option key={u} value={u}>{humanize(u)}</option>)}
                  </select>
                </div>
                {(f.durationValue || f.durationUnit) && <span className="err">{f.durationValue || f.durationUnit}</span>}
              </div>
              <SelectField label="Severity (clinician)" value={a.severity} onChange={(x) => set({ severity: x || null })} options={vocabOpts(v.severities)}
                placeholder="Select" disabled={!canEdit} error={f.severity} />
            </div>
            <Chips label="Symptoms" items={v.symptoms} value={a.symptoms} onChange={(x) => set({ symptoms: x })} disabled={!canEdit} />
            <div className="grid g3">
              <TriState label="Spreading?" value={a.spreading} onChange={(x) => set({ spreading: x })} disabled={!canEdit} />
              <TriState label="Recurring?" value={a.recurring} onChange={(x) => set({ recurring: x })} disabled={!canEdit} />
              <TextField label="Previous episodes" value={a.previousEpisodes ?? ""} inputMode="numeric" disabled={!canEdit} error={f.previousEpisodes}
                onChange={(x) => set({ previousEpisodes: x === "" ? null : Number(x.replace(/\D/g, "")) })} />
            </div>
            <Chips label="Aggravated by" items={v.factors.filter((x) => !["MOISTURISER", "COOLING", "PRIOR_MEDICINE"].includes(x.code))}
              value={a.aggravatingFactors} onChange={(x) => set({ aggravatingFactors: x })} disabled={!canEdit} />
            <Chips label="Relieved by" items={v.factors.filter((x) => ["MOISTURISER", "COOLING", "PRIOR_MEDICINE", "WATER_SOAP", "SCRATCHING", "NOTHING"].includes(x.code))}
              value={a.relievingFactors} onChange={(x) => set({ relievingFactors: x })} disabled={!canEdit} />
          </div>
        </section>

        <section className="panel">
          <div className="panel-head"><h3>Examination</h3></div>
          <div className="panel-body stack">
            <Chips label="Body location" items={v.bodySites} value={a.bodySites} onChange={(x) => set({ bodySites: x })} disabled={!canEdit} />
            <Chips label="Lesion type" items={v.lesionTypes} value={a.lesionTypes} onChange={(x) => set({ lesionTypes: x })} disabled={!canEdit} />
            <Chips label="Colour" items={v.colours} value={a.colours} onChange={(x) => set({ colours: x })} disabled={!canEdit} />
            <div className="grid g3">
              <SelectField label="Configuration" value={a.configuration} onChange={(x) => set({ configuration: x || null })} options={vocabOpts(v.configurations)}
                placeholder="Not specified" disabled={!canEdit} />
              <SelectField label="Distribution" value={a.distribution} onChange={(x) => set({ distribution: x || null })} options={vocabOpts(v.distributions)}
                placeholder="Not specified" disabled={!canEdit} />
              <TextField label="Extent" unit="% BSA" value={a.extentBsaPercent ?? ""} inputMode="decimal" disabled={!canEdit} error={f.extentBsaPercent}
                onChange={(x) => set({ extentBsaPercent: x === "" ? null : Number(x) })} />
            </div>
            <TextArea label="Examination findings (notes)" value={a.findingsNotes} onChange={(x) => set({ findingsNotes: x })} rows={2} disabled={!canEdit} />
          </div>
        </section>

        <section className="panel">
          <div className="panel-head"><h3>Relevant background</h3></div>
          <div className="panel-body stack">
            <div className="grid g2">
              <TextArea label="Other medical conditions" value={a.otherConditions} onChange={(x) => set({ otherConditions: x })} rows={2} disabled={!canEdit}
                hint="Allergies and current medicines are recorded on the patient record and the Notes tab" />
              <TextArea label="Family history" value={a.familyHistory} onChange={(x) => set({ familyHistory: x })} rows={2} disabled={!canEdit}
                hint="e.g. eczema, asthma, psoriasis" />
            </div>
            <div className="grid g3">
              <TextArea label="Previous skin treatments" className="span2" value={a.previousDermTreatments} onChange={(x) => set({ previousDermTreatments: x })} rows={2} disabled={!canEdit} />
              <SelectField label="Response to previous treatment" value={a.previousTreatmentResponse} onChange={(x) => set({ previousTreatmentResponse: x || null })}
                options={v.treatmentResponses} placeholder="Not recorded" disabled={!canEdit} />
            </div>
            <div className="grid g3">
              <SelectField label="Pregnancy / breastfeeding" value={a.pregnancyStatus} onChange={(x) => set({ pregnancyStatus: x || null })} options={pregnancyOpts}
                placeholder="Not recorded" disabled={!canEdit} error={f.pregnancyStatus} hint="Used by the prescription safety check" />
            </div>
          </div>
        </section>
      </div>
      {canEdit && (
        <div className="row" style={{ justifyContent: "flex-end" }}>
          <span className="small muted" aria-live="polite">{dirty ? "Unsaved assessment changes" : res.data?.exists ? "Assessment saved" : ""}</span>
          <Button variant="primary" busy={busy} onClick={save}>Save assessment</Button>
        </div>
      )}
      <PhotosPanel consultationId={consultationId} editable={canEdit} vocab={v} />
    </div>
  );
}
