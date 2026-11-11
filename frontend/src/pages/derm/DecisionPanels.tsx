// Three separate, clearly labelled decision-support panels shown side by side:
//   A · Clinical knowledge      (approved guideline content)
//   B · My prescribing patterns (this doctor's own previous comparable cases)
//   C · ClinicFlow network      (de-identified, threshold-protected cross-clinic patterns)
// Nothing here changes the prescription unless the doctor presses "Add".
import { useCallback, useEffect, useState, type ReactNode } from "react";
import { api, ApiError } from "../../lib/api";
import { useAuth } from "../../lib/auth";
import type { RxItem } from "../../lib/types";
import { CatalogueProduct, PanelA, PatternMed, PatternPanel, SafetyAlert, Suggestion } from "../../lib/derm";
import { humanize } from "../../lib/format";
import { Button } from "../../components/ui";
import { IconAlert } from "../../components/icons";

type Source = "CLINICAL" | "MY_PATTERNS" | "NETWORK";
type State<T> = { loading: boolean; data?: T; error?: ApiError };

export function productLabel(p: CatalogueProduct) {
  return `${p.brandName ? `${p.brandName} (${p.genericName})` : p.genericName} ${p.strength} ${humanize(p.dosageForm).toLowerCase()}`;
}

function startingDose(form: string) {
  const f = form.toUpperCase();
  if (f === "TABLET") return "1 tablet";
  if (f === "CAPSULE") return "1 capsule";
  if (["CREAM", "OINTMENT", "GEL", "LOTION"].includes(f)) return "Apply thin layer";
  if (f === "SHAMPOO") return "Apply to scalp";
  return "";
}

export function itemFrom(p: CatalogueProduct, generic: string, source: Source, snapshotId: string, s?: Suggestion): RxItem {
  return {
    genericCode: generic,
    medicationId: p.medicationId, medicineName: p.genericName, strength: p.strength, dosageForm: p.dosageForm, route: s?.route || p.route,
    dose: s?.dose || p.defaultDose || startingDose(p.dosageForm), frequency: s?.frequency || p.defaultFrequency || "",
    durationDays: s?.durationDays ?? p.defaultDurationDays ?? null,
    sourceKind: source, sourceSnapshotId: snapshotId,
    suggestedDose: s?.dose ?? null, suggestedFrequency: s?.frequency ?? null, suggestedDurationDays: s?.durationDays ?? null,
  };
}

function Alerts({ alerts }: { alerts: SafetyAlert[] }) {
  if (!alerts?.length) return null;
  return (
    <ul className="mini-alerts">
      {alerts.map((a, i) => (
        <li key={i} className={`lvl-${a.level.toLowerCase()}`}><IconAlert /> {a.message}</li>
      ))}
    </ul>
  );
}

function AddButton({ products, onAdd, inRx, label = "Add" }: { products: CatalogueProduct[]; onAdd: (p: CatalogueProduct) => void; inRx: boolean; label?: string }) {
  const [pick, setPick] = useState(0);
  if (!products.length) return <span className="small muted">Not in catalogue</span>;
  if (inRx) return <span className="small badge b-green">In prescription</span>;
  return (
    <span className="row" style={{ gap: 4, flexWrap: "nowrap" }}>
      {products.length > 1 && (
        <select className="select select-sm" aria-label="Product" value={pick} onChange={(e) => setPick(Number(e.target.value))}>
          {products.map((p, i) => <option key={p.medicationId} value={i}>{productLabel(p)}</option>)}
        </select>
      )}
      <Button size="sm" onClick={() => onAdd(products[pick])} title={products.length === 1 ? productLabel(products[0]) : undefined}>{label}</Button>
    </span>
  );
}

function PanelShell({ kind, title, subtitle, children, onRefresh, loading }: {
  kind: "a" | "b" | "c"; title: string; subtitle: string; children: ReactNode; onRefresh?: () => void; loading?: boolean;
}) {
  return (
    <section className={`cds-panel cds-${kind}`} aria-label={title}>
      <header>
        <div>
          <div className="cds-tag">{kind.toUpperCase()}</div>
          <h3>{title}</h3>
          <p>{subtitle}</p>
        </div>
        {onRefresh && <Button size="sm" variant="ghost" busy={loading} onClick={onRefresh}>Refresh</Button>}
      </header>
      <div className="cds-body">{children}</div>
    </section>
  );
}

function StateMessage({ title, children, kind = "neutral" }: { title: string; children?: ReactNode; kind?: "neutral" | "warn" | "error" }) {
  return <div className={`cds-state cds-state-${kind}`}><strong>{title}</strong>{children && <div className="small">{children}</div>}</div>;
}

export function DecisionPanels({ consultationId, ensureSaved, rxGenerics, onAdd, editable }: {
  consultationId: string; ensureSaved: () => Promise<boolean>; rxGenerics: Set<string>; onAdd: (item: RxItem) => void; editable: boolean;
}) {
  const { can } = useAuth();
  const [a, setA] = useState<State<PanelA>>({ loading: false });
  const [b, setB] = useState<State<PatternPanel>>({ loading: false });
  const [c, setC] = useState<State<PatternPanel>>({ loading: false });

  const run = useCallback(async <T,>(path: string, set: (s: State<T>) => void) => {
    set({ loading: true });
    try { set({ loading: false, data: await api.post<T>(`/consultations/${consultationId}/${path}`) }); }
    catch (e) { set({ loading: false, error: e as ApiError }); }
  }, [consultationId]);

  const runAll = useCallback(async () => {
    if (!(await ensureSaved())) return;
    run<PanelA>("cds/clinical", setA);
    run<PatternPanel>("cds/my-patterns", setB);
    if (can("network.read")) run<PatternPanel>("cds/network", setC);
  }, [ensureSaved, run, can]);

  useEffect(() => { if (editable) runAll(); /* eslint-disable-next-line react-hooks/exhaustive-deps */ }, [consultationId]);

  if (!editable) return <p className="muted">Decision support is available to the treating doctor while the consultation is a draft.</p>;

  return (
    <div className="stack">
      <div className="cds-intro">
        <span>Three independent sources, shown side by side. None of them is a diagnosis or a prescription: you decide, and nothing is added unless you press <em>Add</em>.</span>
        <Button size="sm" onClick={runAll} busy={a.loading || b.loading || c.loading}>Save &amp; refresh all</Button>
      </div>
      <div className="cds-grid">
        <PanelShell kind="a" title="Clinical knowledge" subtitle="Approved guideline content matched to the recorded findings" loading={a.loading}
          onRefresh={async () => { if (await ensureSaved()) run<PanelA>("cds/clinical", setA); }}>
          <PanelABody s={a} rxGenerics={rxGenerics} onAdd={onAdd} />
        </PanelShell>
        <PanelShell kind="b" title="My prescribing patterns" subtitle="What you prescribed in your own previous comparable cases" loading={b.loading}
          onRefresh={async () => { if (await ensureSaved()) run<PatternPanel>("cds/my-patterns", setB); }}>
          <PatternBody s={b} source="MY_PATTERNS" rxGenerics={rxGenerics} onAdd={onAdd} />
        </PanelShell>
        <PanelShell kind="c" title="ClinicFlow network" subtitle="De-identified patterns of other doctors at participating clinics" loading={c.loading}
          onRefresh={can("network.read") ? async () => { if (await ensureSaved()) run<PatternPanel>("cds/network", setC); } : undefined}>
          {can("network.read") ? <PatternBody s={c} source="NETWORK" rxGenerics={rxGenerics} onAdd={onAdd} />
            : <StateMessage title="Not available">Your role does not include network insights.</StateMessage>}
        </PanelShell>
      </div>
    </div>
  );
}

function PanelABody({ s, rxGenerics, onAdd }: { s: State<PanelA>; rxGenerics: Set<string>; onAdd: (i: RxItem) => void }) {
  if (s.loading && !s.data) return <div className="cds-loading"><span className="spinner" /> Comparing findings…</div>;
  if (s.error) return <StateMessage kind="error" title="Could not load clinical knowledge">{s.error.message}</StateMessage>;
  const d = s.data;
  if (!d) return null;
  const sugg = (g: string) => d.suggestions.find((x) => x.generic === g);
  return (
    <>
      {d.redFlags.length > 0 && (
        <div className="red-flags" role="alert">
          {d.redFlags.map((f) => (
            <div key={f.code}><strong><IconAlert /> {f.title}</strong><div className="small">{f.guidance}</div>{f.reference && <div className="ref">{f.reference}</div>}</div>
          ))}
        </div>
      )}
      {d.status === "INSUFFICIENT_DATA" && (
        <StateMessage kind="warn" title="Not enough information yet">
          {d.message} Missing: {d.missing.join(", ")}.
        </StateMessage>
      )}
      {d.status === "NO_APPROVED_CONTENT" && <StateMessage kind="warn" title="No approved clinical content">{d.message}</StateMessage>}
      {d.status === "NO_MATCH" && <StateMessage title="No close match">{d.message}</StateMessage>}
      {d.status === "OK" && (
        <>
          <h4>Possible conditions to consider</h4>
          <ol className="cond-list">
            {d.conditions.map((c) => (
              <li key={c.code}>
                <div className="row" style={{ justifyContent: "space-between" }}>
                  <strong>{c.name}</strong>
                  <span className="row" style={{ gap: 4 }}><span className="chip tabular">{c.icd10}</span><span className={`badge ${c.strength.startsWith("Strong") ? "b-blue" : c.strength.startsWith("Moderate") ? "b-amber" : "b-grey"}`}>{c.strength}</span></span>
                </div>
                <details>
                  <summary className="small">Why ({c.matched.length} findings match)</summary>
                  <div className="small">Matches: {c.matched.map((m) => m.finding).join("; ")}</div>
                  {c.notRecorded.length > 0 && <div className="small muted">Typical but not recorded: {c.notRecorded.map((m) => m.finding).join("; ")}</div>}
                  {c.observations.map((o) => <div key={o} className="small">• {o}</div>)}
                  {c.references.map((r) => <div key={r.citation} className="ref">{r.citation}</div>)}
                </details>
              </li>
            ))}
          </ol>
          {d.questions.length > 0 && (<><h4>Questions that help distinguish</h4><ul className="small q-list">{d.questions.map((q) => <li key={q.text}>{q.text} <span className="muted">({q.condition})</span></li>)}</ul></>)}
          {d.treatments.length > 0 && (
            <>
              <h4>Guideline treatment options</h4>
              {d.treatments.map((t, i) => (
                <div className="tx-option" key={i}>
                  <div className="small"><span className="badge b-grey">{t.line ? humanize(t.line) : "Option"}</span> <span className="muted">{t.condition}</span></div>
                  <div>{t.text}</div>
                  {t.medicines.map((m) => (
                    <div key={m.generic} className="tx-med">
                      <span className="small"><strong>{m.products[0]?.genericName ?? m.generic}</strong>
                        {m.products[0]?.commonDosage ? <span className="muted"> · {m.products[0].commonDosage}</span> : m.products[0] && m.products[0].reviewStatus !== "APPROVED" ? <span className="muted"> · dosing information awaiting clinical review</span> : null}
                      </span>
                      <AddButton products={m.products} inRx={rxGenerics.has(m.generic)} onAdd={(p) => onAdd(itemFrom(p, m.generic, "CLINICAL", d.snapshotId, sugg(m.generic)))} />
                      <Alerts alerts={m.patientAlerts} />
                    </div>
                  ))}
                  <div className="ref">{t.reference}</div>
                </div>
              ))}
            </>
          )}
          {d.referrals.length > 0 && (<><h4>When to refer</h4><ul className="small">{d.referrals.map((r) => <li key={r.text}>{r.text}</li>)}</ul></>)}
        </>
      )}
      <p className="cds-disclaimer">{d.disclaimer}</p>
    </>
  );
}

function Outcomes({ m }: { m: PatternMed }) {
  const o = m.outcomes;
  if (!o.shown) return <span className="small muted">{o.recorded} outcome{o.recorded === 1 ? "" : "s"} recorded</span>;
  const pct = (k: string) => (o[k] as { percent: number } | undefined)?.percent ?? 0;
  return (
    <span className="small">
      Follow-up ({o.recorded}): {pct("RESOLVED") + pct("IMPROVED")}% resolved/improved, {pct("UNCHANGED")}% unchanged, {pct("WORSENED")}% worse
    </span>
  );
}

function PatternBody({ s, source, rxGenerics, onAdd }: { s: State<PatternPanel>; source: "MY_PATTERNS" | "NETWORK"; rxGenerics: Set<string>; onAdd: (i: RxItem) => void }) {
  if (s.loading && !s.data) return <div className="cds-loading"><span className="spinner" /> Looking at {source === "NETWORK" ? "network" : "your previous"} cases…</div>;
  if (s.error) return <StateMessage kind="error" title="Could not load patterns">{s.error.message}</StateMessage>;
  const d = s.data;
  if (!d) return null;
  return (
    <>
      {d.status === "NOT_PARTICIPATING" && <StateMessage title="Clinic not in the network">{d.message}</StateMessage>}
      {d.status === "INSUFFICIENT_DATA" && <StateMessage kind="warn" title="Not enough comparable cases">{d.message}</StateMessage>}
      {d.status === "OK" && (
        <>
          <div className="pattern-meta small">
            <div><strong>{d.diagnosis?.display}</strong> <span className="chip tabular">{d.diagnosis?.code}</span></div>
            <div>{d.comparison?.description}{d.comparison?.severity ? `, ${humanize(d.comparison.severity).toLowerCase()}` : ""}{d.comparison?.tier === "DX_SEVERITY_AGE" && d.comparison.ageBand ? `, age ${d.comparison.ageBand}` : ""}</div>
            <div>
              <strong className="tabular">{d.caseCount}</strong> case{d.caseCount === 1 ? "" : "s"}
              {source === "NETWORK" && <> · <strong>{d.doctorCount}</strong> doctors · <strong>{d.clinicCount}</strong> clinics</>}
              {d.period?.from && <> · {d.period.from.slice(0, 7)} to {d.period.to.slice(0, 7)}</>}
            </div>
          </div>
          <table className="pattern-table">
            <thead><tr><th>Medicine</th><th className="num">Share of cases</th><th /></tr></thead>
            <tbody>
              {d.medications.map((m) => {
                const sug = d.suggestions.find((x) => x.generic === m.generic);
                return (
                  <tr key={m.generic}>
                    <td>
                      <strong>{m.name}</strong>
                      <div className="hbar" aria-hidden="true"><span style={{ width: `${Math.min(100, m.percent)}%` }} /></div>
                      <div className="small muted">Usually {m.usualRegimen.frequency || "—"}{m.usualRegimen.durationDays ? ` for ${m.usualRegimen.durationDays} days` : ""} ({m.usualRegimen.cases} of {m.cases})</div>
                      <Outcomes m={m} />
                      <Alerts alerts={m.patientAlerts} />
                    </td>
                    <td className="num tabular"><strong>{m.percent}%</strong><div className="small muted">{m.cases} case{m.cases === 1 ? "" : "s"}</div>{source === "NETWORK" && m.doctors ? <div className="small muted">{m.doctors} doctors</div> : null}</td>
                    <td><AddButton products={m.products} inRx={rxGenerics.has(m.generic)} onAdd={(p) => onAdd(itemFrom(p, m.generic, source, d.snapshotId, sug))} /></td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          {d.otherMedicinesGrouped && <p className="small muted">{d.otherMedicinesGrouped.note}.</p>}
          {source === "NETWORK" && d.thresholds && (
            <p className="small muted">Shown only with at least {d.thresholds.minCases} cases, {d.thresholds.minDoctors} doctors and {d.thresholds.minClinics} clinics, and no clinic above {d.thresholds.maxShareFromOneClinicPercent}% of cases.</p>
          )}
        </>
      )}
      <p className="cds-disclaimer">{d.disclaimer}</p>
    </>
  );
}
