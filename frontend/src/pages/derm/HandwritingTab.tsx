// Handwritten notes tab: the writing pad, "Analyse handwriting", the side-by-side
// review (original ink | editable text + proposed fields) and the read-only record.
import { useEffect, useMemo, useRef, useState } from "react";
import { api, ApiError } from "../../lib/api";
import { dateTime, humanize } from "../../lib/format";
import { replaceWord } from "../../lib/ink";
import { Banner, Button, ErrorBanner, Loading, useAsync, useToast } from "../../components/ui";
import { HandwritingPad, PadHandle, PadPage } from "./HandwritingPad";

// ---------- API shapes ----------
interface Item { code: string; label?: string; evidence?: string | null; fuzzy?: boolean }
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
  medicines: { genericCode: string; name: string; evidence: string; fuzzy: boolean; strength?: string; frequency?: string; durationDays?: number;
    products: { medicationId: string; genericName: string; brandName?: string | null; strength: string; dosageForm: string; route: string }[] }[];
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
export interface AcceptedFields {
  notes: Partial<Record<"chiefComplaint" | "historyPresentIllness" | "examinationNotes" | "treatmentPlan", string>>;
  assessment: Record<string, unknown>;
  diagnoses: { code: string; display: string }[];
  medicines: { medicationId: string; medicineName: string; strength: string; dosageForm: string; route: string; frequency?: string; durationDays?: number }[];
}

const NOTE_FIELDS: [keyof AcceptedFields["notes"], string, keyof Extraction["sections"]][] = [
  ["chiefComplaint", "Chief complaint", "complaint"],
  ["historyPresentIllness", "History of present illness", "history"],
  ["examinationNotes", "Examination findings", "examination"],
  ["treatmentPlan", "Treatment plan / advice", "plan"],
];

// ---------- page images (read-only) ----------
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
  const toast = useToast();
  const ov = useAsync(() => api.get<Overview>(`/consultations/${consultationId}/handwriting`), [consultationId]);
  const [pages, setPages] = useState<PadPage[] | null>(null);
  const [reviewing, setReviewing] = useState<Transcription | null>(null);
  const [analysing, setAnalysing] = useState(false);
  const [err, setErr] = useState<ApiError | null>(null);
  const [justApplied, setJustApplied] = useState(false);
  const pad = useRef<PadHandle>(null);
  useEffect(() => { if (ov.data) setPages(ov.data.pages); }, [ov.data]);

  if (ov.loading && !ov.data) return <Loading label="Loading handwritten notes" />;
  if (ov.error) return <ErrorBanner error={ov.error} />;
  const d = ov.data!;
  const pending = d.transcriptions.find((t) => t.status === "PENDING_REVIEW");
  const accepted = d.transcriptions.filter((t) => t.status === "ACCEPTED");

  const analyse = async () => {
    setAnalysing(true); setErr(null);
    try {
      const t = await api.post<Transcription>(`/consultations/${consultationId}/handwriting/analyse`);
      setReviewing(t);
      if (t.recognitionFailed) toast("Automatic recognition failed. You can type the text instead.");
    } catch (e) { setErr(e as ApiError); } finally { setAnalysing(false); }
  };

  if (reviewing) {
    return (
      <ReviewPanel consultationId={consultationId} tx={reviewing} pages={pages ?? d.pages} recognizer={d.recognizer}
        onCancel={() => { setReviewing(null); ov.reload(); }}
        onDiscard={async () => { await api.post(`/handwriting/transcriptions/${reviewing.id}/discard`); setReviewing(null); ov.reload(); toast("Analysis discarded"); }}
        onAccepted={() => { setReviewing(null); setJustApplied(true); ov.reload(); }}
        onApply={onApply} />
    );
  }

  return (
    <div className="stack">
      {err && <Banner kind="error">{err.message}</Banner>}
      {(justApplied || (d.canEdit && accepted.length > 0)) && (
        <Banner kind="info">
          {justApplied ? "The approved information was added to the consultation." : "Approved handwritten notes have been added to this consultation."}{" "}
          {showDecisionSupport && <Button size="sm" variant="primary" onClick={onOpenDecisionSupport}>Open decision support (A · B · C)</Button>}
        </Banner>
      )}
      {d.canEdit && pending && (
        <Banner kind="warn">An analysis from {dateTime(pending.createdAt)} is waiting for your review.{" "}
          <Button size="sm" onClick={() => setReviewing(pending)}>Continue review</Button></Banner>
      )}
      {d.canEdit ? (
        <>
          <RecognizerNote r={d.recognizer} />
          <HandwritingPad ref={pad} consultationId={consultationId} initialPages={d.pages} onSaved={setPages} onAnalyse={analyse} analysing={analysing} />
        </>
      ) : (
        <section className="panel">
          <div className="panel-head"><h3>Original handwriting</h3></div>
          <div className="panel-body"><PageImages consultationId={consultationId} pages={d.pages} /></div>
        </section>
      )}
      {accepted.map((t) => <ApprovedRecord key={t.id} t={t} />)}
    </div>
  );
}

function RecognizerNote({ r }: { r: Overview["recognizer"] }) {
  if (!r.enabled)
    return (
      <p className="small muted" style={{ margin: 0 }}>
        Automatic handwriting recognition is not set up on this server. <em>Analyse handwriting</em> opens your notes beside a text box, where you
        can type, or write with Apple Pencil (iPadOS Scribble converts it on the iPad). ClinicFlow then organises the text into consultation fields.
      </p>
    );
  return (
    <p className="small muted" style={{ margin: 0 }}>
      Recognition by <strong>{r.description}</strong>.{r.sendsDataExternally && " Page images are sent to this service when you press Analyse."}
    </p>
  );
}

function ApprovedRecord({ t }: { t: Transcription }) {
  const f = t.acceptedFields;
  return (
    <section className="panel">
      <div className="panel-head">
        <h3>Doctor-approved transcription</h3>
        <span className="small muted">Approved by {t.reviewedBy}, {dateTime(t.reviewedAt ?? t.createdAt)} · {t.provider === "NONE" ? "typed by doctor" : `recognised by ${humanize(t.provider)}, corrected by doctor`}</span>
      </div>
      <div className="panel-body stack">
        <pre className="hw-text-record">{t.correctedText}</pre>
        {f && (
          <div className="small">
            <strong>Applied to the consultation:</strong>{" "}
            {[...Object.keys(f.notes ?? {}).map((k) => NOTE_FIELDS.find((n) => n[0] === k)?.[1] ?? k),
              ...(Object.keys(f.assessment ?? {}).length ? ["dermatology assessment findings"] : []),
              ...(f.diagnoses ?? []).map((x) => `${x.code} ${x.display}`),
              ...(f.medicines ?? []).map((m) => `${m.medicineName} (draft prescription)`)].join("; ") || "nothing"}
          </div>
        )}
        {t.recognizedText && t.recognizedText !== t.correctedText && (
          <details><summary className="small">Machine-recognised text before corrections</summary><pre className="hw-text-record muted">{t.recognizedText}</pre></details>
        )}
      </div>
    </section>
  );
}

// ---------- review ----------
type Key = string;
const itemKey = (group: string, code: string) => `${group}:${code}`;

function ReviewPanel({ consultationId, tx, pages, recognizer, onCancel, onDiscard, onAccepted, onApply }: {
  consultationId: string; tx: Transcription; pages: PadPage[]; recognizer: Overview["recognizer"];
  onCancel: () => void; onDiscard: () => void; onAccepted: () => void; onApply: (f: AcceptedFields) => Promise<boolean>;
}) {
  const toast = useToast();
  const [text, setText] = useState(tx.recognizedText ?? "");
  const [ex, setEx] = useState<Extraction | null>(tx.extraction ?? null);
  const [extractedFor, setExtractedFor] = useState(tx.recognizedText ?? "");
  const [extracting, setExtracting] = useState(false);
  const [toggles, setToggles] = useState<Record<Key, boolean>>({});
  const [noteEdits, setNoteEdits] = useState<Record<string, string>>({});
  const [productPick, setProductPick] = useState<Record<string, number>>({});
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const timer = useRef<number | undefined>(undefined);

  const runExtract = async (t: string) => {
    setExtracting(true);
    try { setEx(await api.post<Extraction>(`/consultations/${consultationId}/handwriting/extract`, { text: t })); setExtractedFor(t); }
    catch (e) { setErr((e as ApiError).message); } finally { setExtracting(false); }
  };
  const onText = (t: string) => {
    setText(t);
    window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => runExtract(t), 700);
  };
  useEffect(() => () => window.clearTimeout(timer.current), []);

  // Defaults: exact matches are pre-selected; approximate matches, diagnoses and medicines need an explicit tick.
  const isOn = (k: Key, fuzzy: boolean, defaultOn = true) => toggles[k] ?? (defaultOn && !fuzzy);
  const flip = (k: Key, cur: boolean) => setToggles({ ...toggles, [k]: !cur });
  const noteValue = (f: string, sec: keyof Extraction["sections"]) => noteEdits[f] ?? ex?.sections[sec] ?? "";

  const applyCorrection = (from: string, to: string) => { const t = replaceWord(text, from, to); onText(t); };

  const selection = useMemo((): AcceptedFields | null => {
    if (!ex) return null;
    const a = ex.assessment;
    const sel: AcceptedFields = { notes: {}, assessment: {}, diagnoses: [], medicines: [] };
    for (const [f, , sec] of NOTE_FIELDS) {
      const v = noteValue(f, sec).trim();
      if (v && isOn(itemKey("note", f), false)) sel.notes[f] = v;
    }
    const scalar = (name: string, it: Item | null, value?: unknown) => {
      if (it && isOn(itemKey(name, it.code), !!it.fuzzy)) sel.assessment[name] = value ?? it.code;
    };
    scalar("problemType", a.problemType);
    if (a.duration && isOn(itemKey("duration", "d"), false)) { sel.assessment.durationValue = a.duration.value; sel.assessment.durationUnit = a.duration.unit; }
    scalar("severity", a.severity);
    scalar("configuration", a.configuration);
    scalar("distribution", a.distribution);
    scalar("pregnancyStatus", a.pregnancyStatus);
    if (a.spreading && isOn(itemKey("spreading", "y"), false)) sel.assessment.spreading = true;
    if (a.recurring && isOn(itemKey("recurring", "y"), false)) sel.assessment.recurring = true;
    if (a.extentBsaPercent && isOn(itemKey("extent", "x"), false)) sel.assessment.extentBsaPercent = a.extentBsaPercent.value;
    for (const [name, list] of [["symptoms", a.symptoms], ["bodySites", a.bodySites], ["lesionTypes", a.lesionTypes], ["colours", a.colours],
      ["aggravatingFactors", a.aggravatingFactors]] as [string, Item[]][]) {
      const codes = list.filter((i) => isOn(itemKey(name, i.code), !!i.fuzzy)).map((i) => i.code);
      if (codes.length) sel.assessment[name] = codes;
    }
    for (const dx of ex.diagnoses) if (isOn(itemKey("dx", dx.code), dx.fuzzy, false)) sel.diagnoses.push({ code: dx.code, display: dx.display });
    for (const m of ex.medicines) {
      const k = itemKey("med", m.genericCode);
      const p = m.products[productPick[k] ?? 0];
      if (p && isOn(k, m.fuzzy, false))
        sel.medicines.push({ medicationId: p.medicationId, medicineName: p.brandName ? `${p.brandName} (${p.genericName})` : p.genericName, strength: p.strength,
          dosageForm: p.dosageForm, route: p.route, frequency: m.frequency, durationDays: m.durationDays });
    }
    return sel;
  }, [ex, toggles, noteEdits, productPick]); // eslint-disable-line react-hooks/exhaustive-deps

  const count = selection ? Object.keys(selection.notes).length + Object.keys(selection.assessment).length + selection.diagnoses.length + selection.medicines.length : 0;

  const accept = async () => {
    if (!selection) return;
    if (!text.trim()) { setErr("Enter or correct the text before accepting."); return; }
    if (text !== extractedFor) {
      // The doctor accepts exactly what is on screen: refresh the proposals for the latest edits first.
      window.clearTimeout(timer.current);
      await runExtract(text);
      setErr("The proposed fields were updated for your latest edits. Check them and accept again.");
      return;
    }
    setBusy(true); setErr(null);
    try {
      if (!(await onApply(selection))) { setErr("The consultation could not be saved. Check the highlighted fields on the other tabs."); return; }
      await api.post(`/handwriting/transcriptions/${tx.id}/accept`, { correctedText: text, acceptedFields: selection });
      toast("Approved information added to the consultation");
      onAccepted();
    } catch (e) { setErr((e as ApiError).message); } finally { setBusy(false); }
  };

  const Check = ({ k, on, children, fuzzy, evidence }: { k: Key; on: boolean; children: React.ReactNode; fuzzy?: boolean; evidence?: string | null }) => (
    <label className={`hw-check ${fuzzy ? "fuzzy" : ""}`}>
      <input type="checkbox" checked={on} onChange={() => flip(k, on)} />
      <span>{children}{fuzzy && <span className="badge b-amber" style={{ marginLeft: 6 }}>check spelling</span>}
        {evidence && <span className="hw-evidence">“{evidence}”</span>}</span>
    </label>
  );
  const chipGroup = (name: string, title: string, list: Item[]) => list.length ? (
    <div className="hw-field-group"><div className="hw-field-title">{title}</div>
      {list.map((i) => { const k = itemKey(name, i.code); const on = isOn(k, !!i.fuzzy); return <Check key={k} k={k} on={on} fuzzy={i.fuzzy} evidence={i.evidence}>{i.label ?? i.code}</Check>; })}
    </div>) : null;
  const scalarRow = (name: string, title: string, it: Item | null, label?: string) => {
    if (!it) return null;
    const k = itemKey(name, it.code); const on = isOn(k, !!it.fuzzy);
    return <div className="hw-field-group"><div className="hw-field-title">{title}</div><Check k={k} on={on} fuzzy={it.fuzzy} evidence={it.evidence}>{label ?? it.label ?? humanize(it.code)}</Check></div>;
  };

  const a = ex?.assessment;
  return (
    <div className="stack">
      <div className="row" style={{ justifyContent: "space-between" }}>
        <div>
          <h2 style={{ margin: 0 }}>Review handwritten notes</h2>
          <p className="small muted" style={{ margin: "2px 0 0" }}>Compare the text with your handwriting, correct it, and tick what should be added to the consultation. Nothing is saved to the record until you accept.</p>
        </div>
        <div className="row">
          <Button onClick={onCancel}>Back to writing</Button>
          <Button variant="ghost" onClick={onDiscard}>Discard</Button>
        </div>
      </div>
      {err && <Banner kind="error">{err}</Banner>}
      {tx.recognitionFailed && <Banner kind="warn">Automatic recognition failed: {tx.recognitionError}. Type the text or write it with Apple Pencil in the box.</Banner>}
      {tx.provider === "NONE" && (
        <Banner kind="info">Automatic recognition is not configured. Type the note in the text box, or tap it and write with Apple Pencil: iPadOS Scribble converts it on the iPad.</Banner>
      )}
      <div className="hw-review">
        <section className="panel hw-review-original">
          <div className="panel-head"><h3>Original handwriting</h3></div>
          <div className="panel-body"><PageImages consultationId={consultationId} pages={pages} /></div>
        </section>
        <div className="stack hw-review-right">
          <section className="panel">
            <div className="panel-head">
              <h3>{tx.provider === "NONE" ? "Note text" : "Recognised text"}</h3>
              <span className="small muted">{tx.provider === "NONE" ? "entered by you" : `${recognizer.description} · editable`}{extracting ? " · updating…" : ""}</span>
            </div>
            <div className="panel-body stack">
              <textarea className="textarea hw-textarea" aria-label="Note text" value={text} onChange={(e) => onText(e.target.value)} rows={10}
                placeholder={"e.g.\nC/o itchy wheals on arms and back x 5 days\nNo fever, no lip swelling\nO/E generalised erythematous wheals\nImp: ?urticaria"} />
              {ex && ex.corrections.length > 0 && (
                <div className="hw-corrections">
                  <span className="small"><strong>Possible misread words:</strong></span>
                  {ex.corrections.map((c) => (
                    <button type="button" key={c.original} className="chip-toggle" onClick={() => applyCorrection(c.original, c.suggestion)} title={`Replace "${c.original}" with "${c.suggestion}"`}>
                      {c.original} → <strong>{c.suggestion}</strong>
                    </button>
                  ))}
                </div>
              )}
              {ex?.warnings.filter((w) => !w.startsWith("Some words")).map((w) => <span key={w} className="small muted">{w}</span>)}
            </div>
          </section>

          {ex && a && (
            <section className="panel">
              <div className="panel-head"><h3>Proposed consultation fields</h3><span className="small muted">{count} selected</span></div>
              <div className="panel-body stack">
                <div className="hw-field-section">Consultation notes</div>
                {NOTE_FIELDS.map(([f, label, sec]) => {
                  const v = noteValue(f, sec);
                  if (!v) return null;
                  const k = itemKey("note", f); const on = isOn(k, false);
                  return (
                    <div key={f} className="hw-note-field">
                      <label className="hw-check"><input type="checkbox" checked={on} onChange={() => flip(k, on)} /><span>{label}</span></label>
                      <textarea className="textarea" rows={2} value={v} aria-label={label} onChange={(e) => setNoteEdits({ ...noteEdits, [f]: e.target.value })} />
                    </div>
                  );
                })}
                {ex.sections.other && !ex.sections.complaint && <p className="small muted">Unsorted text is kept in the approved transcription.</p>}

                <div className="hw-field-section">Dermatology assessment</div>
                <div className="hw-fields-grid">
                  {scalarRow("problemType", "Type of problem", a.problemType, a.problemType ? `${a.problemType.label}${a.problemType.inferred ? " (inferred from lesions)" : ""}` : undefined)}
                  {a.duration && (
                    <div className="hw-field-group"><div className="hw-field-title">Duration</div>
                      <Check k={itemKey("duration", "d")} on={isOn(itemKey("duration", "d"), false)} evidence={a.duration.evidence}>{a.duration.value} {humanize(a.duration.unit).toLowerCase()}</Check></div>
                  )}
                  {scalarRow("severity", "Severity", a.severity)}
                  {chipGroup("symptoms", "Symptoms", a.symptoms)}
                  {chipGroup("bodySites", "Affected areas", a.bodySites)}
                  {chipGroup("lesionTypes", "Lesion types", a.lesionTypes)}
                  {chipGroup("colours", "Colour", a.colours)}
                  {scalarRow("configuration", "Configuration", a.configuration)}
                  {scalarRow("distribution", "Distribution", a.distribution)}
                  {a.spreading && <div className="hw-field-group"><div className="hw-field-title">Course</div><Check k={itemKey("spreading", "y")} on={isOn(itemKey("spreading", "y"), false)} evidence={a.spreading.evidence}>Spreading</Check></div>}
                  {a.recurring && <div className="hw-field-group"><div className="hw-field-title">Course</div><Check k={itemKey("recurring", "y")} on={isOn(itemKey("recurring", "y"), false)} evidence={a.recurring.evidence}>Recurring</Check></div>}
                  {chipGroup("aggravatingFactors", "Aggravated by", a.aggravatingFactors)}
                  {a.extentBsaPercent && <div className="hw-field-group"><div className="hw-field-title">Extent</div><Check k={itemKey("extent", "x")} on={isOn(itemKey("extent", "x"), false)} evidence={a.extentBsaPercent.evidence}>{a.extentBsaPercent.value}% BSA</Check></div>}
                  {scalarRow("pregnancyStatus", "Pregnancy", a.pregnancyStatus, a.pregnancyStatus ? humanize(a.pregnancyStatus.code) : undefined)}
                </div>
                {ex.negatives.length > 0 && <p className="small"><strong>Recorded as absent:</strong> {ex.negatives.map((n) => n.label ?? n.code).join(", ")} <span className="muted">(kept in the note text)</span></p>}
                {!a.symptoms.length && !a.lesionTypes.length && !a.bodySites.length && <p className="small muted">No dermatology findings recognised yet.</p>}

                <div className="hw-field-section">Possible diagnoses you wrote</div>
                {ex.diagnoses.length === 0 ? <p className="small muted">None recognised.</p> : ex.diagnoses.map((dx) => {
                  const k = itemKey("dx", dx.code); const on = isOn(k, dx.fuzzy, false);
                  return <Check key={k} k={k} on={on} fuzzy={dx.fuzzy} evidence={dx.evidence}><span className="chip tabular">{dx.code}</span> {dx.display} <span className="muted">· {dx.writtenAs === "query" ? "written as query, added as provisional" : "added as provisional"}</span></Check>;
                })}
                {ex.excludedDiagnoses.length > 0 && <p className="small muted">Written as excluded: {ex.excludedDiagnoses.map((d) => d.display).join(", ")}</p>}

                <div className="hw-field-section">Medicines mentioned</div>
                {ex.medicines.length === 0 ? <p className="small muted">None recognised.</p> : ex.medicines.map((m) => {
                  const k = itemKey("med", m.genericCode); const on = isOn(k, m.fuzzy, false);
                  return (
                    <div key={k} className="hw-med">
                      <Check k={k} on={on} fuzzy={m.fuzzy} evidence={m.evidence}>
                        <strong>{m.name}</strong>{m.strength ? ` ${m.strength}` : ""}{m.frequency ? ` · ${m.frequency}` : ""}{m.durationDays ? ` · ${m.durationDays} days` : ""}
                      </Check>
                      {m.products.length > 1 && (
                        <select className="select select-sm" aria-label={`Product for ${m.name}`} value={productPick[k] ?? 0} onChange={(e) => setProductPick({ ...productPick, [k]: Number(e.target.value) })}>
                          {m.products.map((p, i) => <option key={p.medicationId} value={i}>{p.brandName ? `${p.brandName} (${p.genericName})` : p.genericName} {p.strength} {humanize(p.dosageForm).toLowerCase()}</option>)}
                        </select>
                      )}
                    </div>
                  );
                })}
                {ex.medicines.length > 0 && <p className="small muted">Ticked medicines are added to the <em>draft</em> prescription for you to check dose, frequency and duration. Nothing is finalized.</p>}
              </div>
            </section>
          )}
          <div className="row" style={{ justifyContent: "flex-end" }}>
            <span className="small muted">You remain responsible for every item added. Diagnoses are added as provisional.</span>
            <Button variant="primary" busy={busy} disabled={!text.trim() || extracting} onClick={accept}>Accept and add to consultation ({count})</Button>
          </div>
        </div>
      </div>
    </div>
  );
}
