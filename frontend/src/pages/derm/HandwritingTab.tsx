// Handwritten notes tab, kept as simple as paper: one screen, two sections.
//   1. Write  - the pad. Pages save themselves.
//   2. Text   - the note as text (converted automatically when a recognition service is set up,
//               otherwise written with Apple Pencil Scribble or typed), and below it a short list of
//               what will go into the consultation. One button adds it.
// Nothing reaches the consultation until the doctor presses "Add to consultation"; medicines go to
// the draft prescription and diagnoses are provisional. The doctor can leave any item out.
import { useEffect, useRef, useState } from "react";
import { api, ApiError } from "../../lib/api";
import { dateTime, humanize, time } from "../../lib/format";
import { replaceWord } from "../../lib/ink";
import { Banner, Button, ErrorBanner, Loading, useAsync, useToast } from "../../components/ui";
import { HandwritingPad, PadHandle, PadPage } from "./HandwritingPad";

// ---------- API shapes ----------
interface Item { code: string; label?: string; evidence?: string | null; fuzzy?: boolean }
export interface Product { medicationId: string; genericName: string; brandName?: string | null; strength: string; dosageForm: string; route: string }
export interface MedicineMention {
  genericCode: string | null; name: string; evidence: string; fuzzy: boolean; unmatched?: boolean; strength?: string; frequency?: string;
  durationDays?: number; dosageForm?: string; route?: string; products: Product[];
}
export interface Extraction {
  sections: Partial<Record<"complaint" | "history" | "examination" | "diagnosis" | "plan" | "other", string>>;
  assessment: {
    problemType: (Item & { inferred: boolean }) | null; duration: { value: number; unit: string; evidence: string } | null;
    severity: Item | null; symptoms: Item[]; bodySites: Item[]; lesionTypes: Item[]; colours: Item[];
    configuration: Item | null; distribution: Item | null; spreading: { value: boolean; evidence: string } | null;
    recurring: { value: boolean; evidence: string } | null; aggravatingFactors: Item[];
    extentBsaPercent: { value: number; evidence: string } | null; pregnancyStatus: Item | null;
  };
  negatives: Item[];
  diagnoses: { code: string; display: string; evidence: string; fuzzy: boolean; writtenAs: "query" | "stated" }[];
  excludedDiagnoses: { code: string; display: string; evidence: string }[];
  medicines: MedicineMention[];
  corrections: { original: string; suggestion: string; category: string }[];
  warnings: string[];
}
export interface Transcription {
  id: string; provider: string; providerDetail?: string | null; recognizedText?: string | null; correctedText?: string | null;
  extraction?: Extraction | null; acceptedFields?: AcceptedFields | null; status: "PENDING_REVIEW" | "ACCEPTED" | "DISCARDED";
  createdBy: string; createdAt: string; reviewedBy?: string | null; reviewedAt?: string | null;
  recognitionFailed?: boolean; recognitionError?: string;
}
interface Overview {
  pages: (PadPage & { updatedAt: string })[]; transcriptions: Transcription[]; canEdit: boolean;
  recognizer: { provider: string; description: string; enabled: boolean; sendsDataExternally: boolean };
}
export interface AcceptedMedicine {
  medicationId?: string | null; medicineName: string; strength?: string; dosageForm?: string; route?: string;
  frequency?: string; durationDays?: number; writtenAs: string;
}
export interface AcceptedFields {
  notes: Partial<Record<"chiefComplaint" | "historyPresentIllness" | "examinationNotes" | "treatmentPlan", string>>;
  assessment: Record<string, unknown>;
  diagnoses: { code: string; display: string }[];
  medicines: AcceptedMedicine[];
}

const NOTE_FIELDS: [keyof AcceptedFields["notes"], string, (keyof Extraction["sections"])[]][] = [
  ["chiefComplaint", "Complaint", ["complaint"]],
  ["historyPresentIllness", "History / notes", ["history", "other"]],
  ["examinationNotes", "Examination", ["examination"]],
  ["treatmentPlan", "Treatment plan", ["plan"]],
];
const draftKey = (id: string) => `cf.hw.text.${id}`;
const loadDraft = (id: string) => { try { return sessionStorage.getItem(draftKey(id)); } catch { return null; } };
const saveDraft = (id: string, t: string | null) => {
  try { if (t === null) sessionStorage.removeItem(draftKey(id)); else sessionStorage.setItem(draftKey(id), t); } catch { /* private mode */ }
};

// ---------- what will be added ----------
type Toggles = Record<string, boolean>;
const medKey = (m: MedicineMention, i: number) => `med:${m.genericCode ?? m.name.toLowerCase()}:${i}`;

function pickProduct(m: MedicineMention, choice?: number): Product | undefined {
  if (choice !== undefined) return m.products[choice];
  // Prefer the strength the doctor wrote, then the generic product.
  const want = m.strength?.replace(/\s+/g, "").toLowerCase();
  return (want && m.products.find((p) => p.strength.replace(/\s+/g, "").toLowerCase().startsWith(want))) || m.products[0];
}

export function buildSelection(ex: Extraction, on: (key: string, fuzzy?: boolean) => boolean, picks: Record<string, number>): AcceptedFields {
  const sel: AcceptedFields = { notes: {}, assessment: {}, diagnoses: [], medicines: [] };
  for (const [field, , secs] of NOTE_FIELDS) {
    const v = secs.map((s) => ex.sections[s]?.trim()).filter(Boolean).join("\n");
    if (v && on(`note:${field}`)) sel.notes[field] = v;
  }
  const a = ex.assessment;
  const scalar = (name: string, it: Item | null) => { if (it && on(`a:${name}:${it.code}`, it.fuzzy)) sel.assessment[name] = it.code; };
  scalar("problemType", a.problemType);
  scalar("severity", a.severity);
  scalar("configuration", a.configuration);
  scalar("distribution", a.distribution);
  scalar("pregnancyStatus", a.pregnancyStatus);
  if (a.duration && on("a:duration")) { sel.assessment.durationValue = a.duration.value; sel.assessment.durationUnit = a.duration.unit; }
  if (a.spreading && on("a:spreading")) sel.assessment.spreading = true;
  if (a.recurring && on("a:recurring")) sel.assessment.recurring = true;
  if (a.extentBsaPercent && on("a:extent")) sel.assessment.extentBsaPercent = a.extentBsaPercent.value;
  for (const [name, list] of listGroups(a)) {
    const codes = list.filter((i) => on(`a:${name}:${i.code}`, i.fuzzy)).map((i) => i.code);
    if (codes.length) sel.assessment[name] = codes;
  }
  for (const dx of ex.diagnoses) if (on(`dx:${dx.code}`, dx.fuzzy)) sel.diagnoses.push({ code: dx.code, display: dx.display });
  ex.medicines.forEach((m, i) => {
    const k = medKey(m, i);
    if (!on(k, m.fuzzy)) return;
    const p = pickProduct(m, picks[k]);
    if (p) sel.medicines.push({ medicationId: p.medicationId, medicineName: p.brandName ? `${p.brandName} (${p.genericName})` : p.genericName,
      strength: p.strength, dosageForm: p.dosageForm, route: p.route, frequency: m.frequency, durationDays: m.durationDays, writtenAs: m.evidence });
    else if (m.route) sel.medicines.push({ medicationId: null, medicineName: m.name, strength: m.strength, dosageForm: m.dosageForm, route: m.route,
      frequency: m.frequency, durationDays: m.durationDays, writtenAs: m.evidence });
  });
  return sel;
}
function listGroups(a: Extraction["assessment"]): [string, Item[], string][] {
  return [["symptoms", a.symptoms, "Symptom"], ["bodySites", a.bodySites, "Area"], ["lesionTypes", a.lesionTypes, "Lesion"],
    ["colours", a.colours, "Colour"], ["aggravatingFactors", a.aggravatingFactors, "Trigger"]];
}
const countOf = (s: AcceptedFields) => Object.keys(s.notes).length + Object.keys(s.assessment).length + s.diagnoses.length + s.medicines.length;

// ---------- page images (read-only record) ----------
function PageImages({ consultationId, pages }: { consultationId: string; pages: PadPage[] }) {
  const [urls, setUrls] = useState<Record<number, string>>({});
  useEffect(() => {
    const made: string[] = [];
    let cancelled = false;
    (async () => {
      for (const p of pages.filter((x) => x.strokeCount > 0)) {
        try {
          const u = await api.blobUrl(`/consultations/${consultationId}/handwriting/pages/${p.pageNo}/image`);
          made.push(u);
          if (!cancelled) setUrls((m) => ({ ...m, [p.pageNo]: u }));
        } catch { /* shown as unavailable */ }
      }
    })();
    return () => { cancelled = true; made.forEach((u) => URL.revokeObjectURL(u)); };
  }, [consultationId, pages.map((p) => `${p.pageNo}:${p.version}`).join(",")]); // eslint-disable-line react-hooks/exhaustive-deps
  const written = pages.filter((x) => x.strokeCount > 0);
  if (!written.length) return <p className="small muted">No handwriting on this consultation.</p>;
  return (
    <div className="hw-images">
      {written.map((p) => (
        <figure key={p.pageNo}>
          {urls[p.pageNo] ? <img src={urls[p.pageNo]} alt={`Handwritten page ${p.pageNo}`} /> : <div className="hw-img-loading"><span className="spinner" /></div>}
          <figcaption className="small muted">Page {p.pageNo}</figcaption>
        </figure>
      ))}
    </div>
  );
}

// ---------- tab ----------
export function HandwritingTab({ consultationId, onApply, onOpenDecisionSupport, showDecisionSupport }: {
  consultationId: string; onApply: (f: AcceptedFields) => Promise<boolean>; onOpenDecisionSupport: () => void; showDecisionSupport: boolean;
}) {
  const ov = useAsync(() => api.get<Overview>(`/consultations/${consultationId}/handwriting`), [consultationId]);
  if (ov.loading && !ov.data) return <Loading label="Loading handwritten notes" />;
  if (ov.error) return <ErrorBanner error={ov.error} />;
  const d = ov.data!;
  const accepted = d.transcriptions.filter((t) => t.status === "ACCEPTED");
  if (!d.canEdit) {
    const last = accepted[0];  // newest first
    return (
      <div className="hw-split">
        <section className="panel hw-split-write">
          <div className="panel-head"><h3>Handwriting</h3></div>
          <div className="panel-body"><PageImages consultationId={consultationId} pages={d.pages} /></div>
        </section>
        <section className="panel hw-split-text">
          <div className="panel-head"><h3>Text</h3>{last && <span className="small muted">Approved by {last.reviewedBy}, {dateTime(last.reviewedAt ?? last.createdAt)}</span>}</div>
          <div className="panel-body">{last ? <pre className="hw-text-record">{last.correctedText}</pre> : <p className="small muted">No approved text.</p>}</div>
        </section>
      </div>
    );
  }
  return <Editor key={consultationId} consultationId={consultationId} ov={d} reload={ov.reload} onApply={onApply}
    onOpenDecisionSupport={onOpenDecisionSupport} showDecisionSupport={showDecisionSupport} />;
}

function Editor({ consultationId, ov, reload, onApply, onOpenDecisionSupport, showDecisionSupport }: {
  consultationId: string; ov: Overview; reload: () => void; onApply: (f: AcceptedFields) => Promise<boolean>;
  onOpenDecisionSupport: () => void; showDecisionSupport: boolean;
}) {
  const toast = useToast();
  const rec = ov.recognizer;
  const pad = useRef<PadHandle>(null);
  const pending = ov.transcriptions.find((t) => t.status === "PENDING_REVIEW") ?? null;
  const lastAccepted = ov.transcriptions.find((t) => t.status === "ACCEPTED") ?? null;  // newest first

  const [text, setTextState] = useState<string>(() => loadDraft(consultationId) ?? pending?.recognizedText ?? lastAccepted?.correctedText ?? "");
  const [typedByDoctor, setTypedByDoctor] = useState<boolean>(() => loadDraft(consultationId) !== null);
  const [ex, setEx] = useState<Extraction | null>(null);
  const [extractedFor, setExtractedFor] = useState<string | null>(null);
  const [toggles, setToggles] = useState<Toggles>({});
  const [picks, setPicks] = useState<Record<string, number>>({});
  const [tx, setTx] = useState<Transcription | null>(pending);
  const [hasInk, setHasInk] = useState(ov.pages.some((p) => p.strokeCount > 0));
  const [inkVersion, setInkVersion] = useState(0);          // bumps on every saved change to the pages
  const [txInkVersion, setTxInkVersion] = useState(-1);     // ink version the current transcription records
  const [converting, setConverting] = useState(false);
  const [newerConversion, setNewerConversion] = useState<string | null>(null);
  const [recognitionError, setRecognitionError] = useState<string | null>(null);
  const [addedAt, setAddedAt] = useState<string | null>(lastAccepted && !pending ? (lastAccepted.reviewedAt ?? lastAccepted.createdAt) : null);
  const [addedInkVersion, setAddedInkVersion] = useState(0);
  const [addedText, setAddedText] = useState<string | null>(lastAccepted && !pending ? lastAccepted.correctedText ?? null : null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const exTimer = useRef<number | undefined>(undefined);
  const convTimer = useRef<number | undefined>(undefined);
  const textRef = useRef(text); textRef.current = text;
  const typedRef = useRef(typedByDoctor); typedRef.current = typedByDoctor;

  const runExtract = async (t: string): Promise<Extraction | null> => {
    if (!t.trim()) { setEx(null); setExtractedFor(t); return null; }
    try {
      const r = await api.post<Extraction>(`/consultations/${consultationId}/handwriting/extract`, { text: t });
      if (t === textRef.current) { setEx(r); setExtractedFor(t); }
      return r;
    } catch (e) { setErr((e as ApiError).message); return null; }
  };
  const setText = (t: string, byDoctor: boolean) => {
    setTextState(t); textRef.current = t;
    if (byDoctor) { setTypedByDoctor(true); saveDraft(consultationId, t); }
    window.clearTimeout(exTimer.current);
    exTimer.current = window.setTimeout(() => runExtract(t), 600);
  };
  useEffect(() => { if (text.trim()) runExtract(text); return () => { window.clearTimeout(exTimer.current); window.clearTimeout(convTimer.current); }; }, []); // eslint-disable-line react-hooks/exhaustive-deps

  // Conversion: one call records the current pages (and, with a recognition service, reads them).
  const convert = async (updateText = true): Promise<Transcription | null> => {
    setConverting(true); setRecognitionError(null);
    const version = inkVersion;
    try {
      const t = await api.post<Transcription>(`/consultations/${consultationId}/handwriting/analyse`);
      setTx(t); setTxInkVersion(version);
      if (t.recognitionFailed) setRecognitionError(t.recognitionError ?? "The recognition service did not respond.");
      else if (updateText && rec.enabled && t.recognizedText != null) {
        if (!typedRef.current || !textRef.current.trim()) { setText(t.recognizedText, false); setNewerConversion(null); }
        else if (t.recognizedText !== textRef.current) setNewerConversion(t.recognizedText);
      }
      return t;
    } catch (e) { setErr((e as ApiError).message); return null; } finally { setConverting(false); }
  };
  const onPadSaved = (pages: PadPage[]) => {
    setHasInk(pages.some((p) => p.strokeCount > 0));
    setInkVersion((v) => v + 1);
    if (rec.enabled) {
      window.clearTimeout(convTimer.current);
      convTimer.current = window.setTimeout(() => { convert(); }, 1500);
    }
  };

  const on = (key: string, fuzzy?: boolean) => toggles[key] ?? !fuzzy;
  const flip = (key: string, fuzzy?: boolean) => setToggles({ ...toggles, [key]: !on(key, fuzzy) });
  const selection = ex ? buildSelection(ex, on, picks) : null;
  const count = selection ? countOf(selection) : 0;
  const upToDate = extractedFor === text;
  const changedSinceAdd = addedAt !== null && (text !== addedText || inkVersion !== addedInkVersion);

  const add = async () => {
    setErr(null);
    if (!text.trim()) { setErr("There is no text yet. Write it in the Text box (Apple Pencil works there) or type it."); return; }
    setBusy(true);
    try {
      if (!(await pad.current?.flush() ?? true)) { setErr("Your handwriting could not be saved. Check the connection and try again."); return; }
      let t = tx;
      if (!t || t.status !== "PENDING_REVIEW" || txInkVersion !== inkVersion) t = await convert(false);
      if (!t) { if (!hasInk) setErr("Write on the page first: the handwriting is kept with the text."); return; }
      const fresh = upToDate && ex ? ex : await runExtract(text);
      if (!fresh) return;
      const sel = buildSelection(fresh, on, picks);
      if (!(await onApply(sel))) { setErr("The consultation could not be saved. Open the other tabs to see what needs attention."); return; }
      await api.post(`/handwriting/transcriptions/${t.id}/accept`, { correctedText: text, acceptedFields: sel });
      setAddedAt(new Date().toISOString()); setAddedText(text); setAddedInkVersion(inkVersion);
      setTx(null); setTxInkVersion(-1);
      saveDraft(consultationId, null);
      toast(`Added to the consultation: ${countOf(sel)} item${countOf(sel) === 1 ? "" : "s"}`);
      reload();
    } catch (e) { setErr((e as ApiError).message); } finally { setBusy(false); }
  };

  return (
    <div className="hw-split">
      <section className="panel hw-split-write" aria-labelledby="hw-write-title">
        <div className="panel-head"><h3 id="hw-write-title">Write</h3></div>
        <div className="panel-body">
          <HandwritingPad ref={pad} consultationId={consultationId} initialPages={ov.pages} onSaved={onPadSaved} />
        </div>
      </section>

      <section className="panel hw-split-text" aria-labelledby="hw-text-title">
        <div className="panel-head">
          <h3 id="hw-text-title">Text</h3>
          <span className="small muted" aria-live="polite">
            {converting ? "Converting…" : rec.enabled ? (typedByDoctor ? "Edited by you" : text ? "Converted from your handwriting" : "Appears as you write") : ""}
          </span>
        </div>
        <div className="panel-body stack">
          {rec.enabled && (
            <p className="small muted hw-how">Converted automatically by {rec.description}{rec.sendsDataExternally ? "; page images are sent to this service" : ""}. Check the text before adding.</p>
          )}
          {!rec.enabled && (
            <p className="small muted hw-how">Write here with Apple Pencil (iPad turns it into text) or type. Automatic conversion of the page is not switched on.</p>
          )}
          {recognitionError && <Banner kind="warn">Conversion failed: {recognitionError}. You can write or type the text here.</Banner>}
          {newerConversion !== null && (
            <div className="hw-newer small">You changed the handwriting after editing the text.{" "}
              <button type="button" className="link-btn" onClick={() => { setText(newerConversion, false); setTypedByDoctor(false); saveDraft(consultationId, null); setNewerConversion(null); }}>
                Replace the text with the new conversion</button>
            </div>
          )}
          <textarea className="textarea hw-textarea" aria-label="Note text" value={text} rows={7}
            onChange={(e) => setText(e.target.value, true)}
            placeholder={"Benzoyl peroxide gel twice a day for 15 days\nApply betadine solution on face every evening for 10 days"} />
          {ex && ex.corrections.length > 0 && (
            <div className="hw-corrections">
              {ex.corrections.map((c) => (
                <button type="button" key={c.original} className="chip-toggle" onClick={() => setText(replaceWord(text, c.original, c.suggestion), true)}>
                  {c.original} → <strong>{c.suggestion}</strong>
                </button>
              ))}
            </div>
          )}

          {ex && selection && <AddList ex={ex} on={on} flip={flip} picks={picks} setPicks={setPicks} />}

          {err && <Banner kind="error">{err}</Banner>}
          {addedAt && !changedSinceAdd ? (
            <div className="hw-added">
              <span>✓ Added to the consultation at {time(addedAt)}</span>
              {showDecisionSupport && <Button size="sm" onClick={onOpenDecisionSupport}>Decision support</Button>}
            </div>
          ) : (
            <div className="hw-add-row">
              <Button variant="primary" busy={busy} disabled={!text.trim() || converting} onClick={add}>
                {addedAt ? "Add changes to consultation" : "Add to consultation"}{count ? ` (${count})` : ""}
              </Button>
              <span className="small muted">Medicines go to the draft prescription; diagnoses are provisional. Nothing is finalized.</span>
            </div>
          )}
        </div>
      </section>
    </div>
  );
}

// ---------- the short list of what will be added ----------
function AddList({ ex, on, flip, picks, setPicks }: {
  ex: Extraction; on: (k: string, fuzzy?: boolean) => boolean; flip: (k: string, fuzzy?: boolean) => void;
  picks: Record<string, number>; setPicks: (p: Record<string, number>) => void;
}) {
  const a = ex.assessment;
  const Row = ({ k, fuzzy, kind, children }: { k: string; fuzzy?: boolean; kind: string; children: React.ReactNode }) => {
    const checked = on(k, fuzzy);
    return (
      <label className={`hw-row ${checked ? "" : "off"}`}>
        <input type="checkbox" checked={checked} onChange={() => flip(k, fuzzy)} />
        <span className="hw-kind">{kind}</span>
        <span className="hw-row-body">{children}{fuzzy && <span className="badge b-amber">check spelling</span>}</span>
      </label>
    );
  };
  const chips: { k: string; label: string; fuzzy?: boolean }[] = [];
  if (a.problemType) chips.push({ k: `a:problemType:${a.problemType.code}`, label: a.problemType.label ?? humanize(a.problemType.code), fuzzy: a.problemType.fuzzy });
  if (a.duration) chips.push({ k: "a:duration", label: `${a.duration.value} ${humanize(a.duration.unit).toLowerCase()}` });
  for (const [name, list] of listGroups(a)) for (const i of list) chips.push({ k: `a:${name}:${i.code}`, label: i.label ?? humanize(i.code), fuzzy: i.fuzzy });
  for (const [name, it] of [["severity", a.severity], ["configuration", a.configuration], ["distribution", a.distribution], ["pregnancyStatus", a.pregnancyStatus]] as [string, Item | null][])
    if (it) chips.push({ k: `a:${name}:${it.code}`, label: it.label ?? humanize(it.code), fuzzy: it.fuzzy });
  if (a.spreading) chips.push({ k: "a:spreading", label: "Spreading" });
  if (a.recurring) chips.push({ k: "a:recurring", label: "Recurring" });
  if (a.extentBsaPercent) chips.push({ k: "a:extent", label: `${a.extentBsaPercent.value}% BSA` });
  const notes = NOTE_FIELDS.map(([f, label, secs]) => [f, label, secs.map((s) => ex.sections[s]?.trim()).filter(Boolean).join(" · ")] as const).filter(([, , v]) => v);
  const nothing = !ex.medicines.length && !ex.diagnoses.length && !chips.length && !notes.length;

  return (
    <div className="hw-addlist" aria-label="Will be added to the consultation">
      <div className="hw-addlist-title">Will be added <span className="muted">· untick anything to leave it out</span></div>
      {nothing && <p className="small muted">Nothing recognised yet.</p>}
      {ex.medicines.map((m, i) => {
        const k = medKey(m, i);
        const p = pickProduct(m, picks[k]);
        if (!p && !m.route)
          return (
            <div key={k} className="hw-row off">
              <span className="hw-check-spacer" />
              <span className="hw-kind">Rx</span>
              <span className="hw-row-body"><strong>{m.name}</strong> <span className="muted">· not in the catalogue and no route written: it stays in the treatment plan. Add it on the Prescription tab.</span></span>
            </div>
          );
        return (
          <Row key={k} k={k} fuzzy={m.fuzzy} kind="Rx">
            <strong>{p ? (p.brandName ?? p.genericName) : m.name}</strong>{" "}
            <span className="muted">{p ? `${p.strength} ${humanize(p.dosageForm).toLowerCase()}` : [m.strength, m.dosageForm && humanize(m.dosageForm).toLowerCase()].filter(Boolean).join(" ")}</span>
            <span className="hw-rx-detail">{m.frequency ?? <em className="hw-missing">how often?</em>} · {m.durationDays ? `${m.durationDays} days` : <em className="hw-missing">how long?</em>}</span>
            {!p && <span className="badge">not in catalogue · added as written</span>}
            {m.products.length > 1 && (
              <select className="select select-sm" aria-label={`Product for ${m.name}`} value={picks[k] ?? m.products.indexOf(p!)}
                onChange={(e) => setPicks({ ...picks, [k]: Number(e.target.value) })}>
                {m.products.map((x, j) => <option key={x.medicationId} value={j}>{x.brandName ? `${x.brandName} (${x.genericName})` : x.genericName} {x.strength} {humanize(x.dosageForm).toLowerCase()}</option>)}
              </select>
            )}
          </Row>
        );
      })}
      {ex.diagnoses.map((dx) => (
        <Row key={dx.code} k={`dx:${dx.code}`} fuzzy={dx.fuzzy} kind="Dx">{dx.display} <span className="muted">· {dx.code} · provisional</span></Row>
      ))}
      {chips.length > 0 && (
        <div className="hw-row hw-row-chips">
          <span className="hw-kind">Findings</span>
          <span className="hw-chips">
            {chips.map((c) => (
              <button type="button" key={c.k} className={`hw-chip ${on(c.k, c.fuzzy) ? "on" : ""} ${c.fuzzy ? "fuzzy" : ""}`} aria-pressed={on(c.k, c.fuzzy)} onClick={() => flip(c.k, c.fuzzy)}>
                {c.label}
              </button>
            ))}
          </span>
        </div>
      )}
      {notes.map(([f, label, v]) => (
        <Row key={f} k={`note:${f}`} kind="Notes"><span className="muted">{label}:</span> <span className="hw-note-preview">{v}</span></Row>
      ))}
      {ex.negatives.length > 0 && <p className="small muted" style={{ margin: 0 }}>Written as absent: {ex.negatives.map((n) => n.label ?? n.code).join(", ")}</p>}
    </div>
  );
}
