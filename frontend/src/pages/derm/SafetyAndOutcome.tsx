// Prescription safety alerts, the override dialog, and (after finalization) the
// record of what was suggested vs. prescribed plus follow-up outcomes.
import { useState } from "react";
import { api, ApiError } from "../../lib/api";
import { useAuth } from "../../lib/auth";
import { DecisionRecord, SafetyAlert, SOURCE_LABEL } from "../../lib/derm";
import { dateTime, humanize } from "../../lib/format";
import { Banner, Button, Modal, SelectField, TextArea, useAsync, useToast } from "../../components/ui";
import { IconAlert } from "../../components/icons";

const LEVEL_CLASS: Record<string, string> = { CONTRAINDICATED: "b-red", WARNING: "b-amber", INFO: "b-grey" };

export function AlertList({ alerts }: { alerts: SafetyAlert[] }) {
  return (
    <ul className="alert-list">
      {alerts.map((a, i) => (
        <li key={i}>
          <span className={`badge ${LEVEL_CLASS[a.level]}`}>{humanize(a.level)}</span>
          <span>{a.message}{a.reference && <span className="ref"> {a.reference}</span>}</span>
        </li>
      ))}
    </ul>
  );
}

export function SafetyPanel({ consultationId, refreshKey }: { consultationId: string; refreshKey: number }) {
  const res = useAsync(() => api.post<{ alerts: SafetyAlert[]; requiresOverride: boolean; note: string }>(`/consultations/${consultationId}/safety-check`), [consultationId, refreshKey]);
  if (res.loading && !res.data) return <div className="panel"><div className="panel-body small muted">Checking prescription safety…</div></div>;
  if (res.error) return <Banner kind="warn">Safety check unavailable: {res.error.message}</Banner>;
  const d = res.data!;
  return (
    <div className={`panel safety-panel ${d.alerts.some((a) => a.level === "CONTRAINDICATED") ? "has-ci" : ""}`}>
      <div className="panel-head">
        <h3><IconAlert /> Prescription safety check</h3>
        <span className="small muted">of the saved draft</span>
      </div>
      <div className="panel-body">
        {d.alerts.length === 0 ? <p className="small" style={{ margin: 0 }}>No alerts for the saved prescription.</p> : <AlertList alerts={d.alerts} />}
        <p className="small muted" style={{ margin: "8px 0 0" }}>{d.note}</p>
      </div>
    </div>
  );
}

export function SafetyOverrideModal({ alerts, onCancel, onConfirm, busy }: { alerts: SafetyAlert[]; onCancel: () => void; onConfirm: (reason: string) => void; busy?: boolean }) {
  const [reason, setReason] = useState("");
  return (
    <Modal title="Safety alerts need your decision" onClose={onCancel}
      footer={<><Button onClick={onCancel}>Go back and change the prescription</Button>
        <Button variant="danger" busy={busy} disabled={reason.trim().length < 5} onClick={() => onConfirm(reason.trim())}>Finalize with documented reason</Button></>}>
      <p>The prescription triggers alerts that must be acknowledged before it is finalized.</p>
      <AlertList alerts={alerts} />
      <TextArea label="Clinical reason for proceeding" required value={reason} onChange={setReason} rows={3}
        hint="Recorded with your name and time in the consultation record and the audit log" />
    </Modal>
  );
}

const DECISION_CLASS: Record<string, string> = { ACCEPTED: "b-green", MODIFIED: "b-amber", NOT_USED: "b-grey", DOCTOR_ADDED: "b-blue" };

export function DecisionRecordPanel({ consultationId }: { consultationId: string }) {
  const { can } = useAuth();
  const toast = useToast();
  const res = useAsync(() => api.get<DecisionRecord>(`/consultations/${consultationId}/decisions`), [consultationId]);
  const [outcome, setOutcome] = useState("");
  const [adverse, setAdverse] = useState("");
  const [notes, setNotes] = useState("");
  const [err, setErr] = useState<ApiError | null>(null);
  const [busy, setBusy] = useState(false);
  const d = res.data;
  const record = async () => {
    setBusy(true); setErr(null);
    try {
      await api.post(`/consultations/${consultationId}/outcome`, { outcome, adverseEffects: adverse || null, notes: notes || null });
      setOutcome(""); setAdverse(""); setNotes(""); res.reload(); toast("Outcome recorded");
    } catch (e) { setErr(e as ApiError); } finally { setBusy(false); }
  };
  return (
    <div className="split">
      <section className="panel">
        <div className="panel-head"><h3>Suggestions vs final prescription</h3></div>
        <div className="panel-body">
          {!d ? <span className="small muted">Loading…</span> : d.decisions.length === 0 ? (
            <p className="small muted" style={{ margin: 0 }}>No decision-support panels were used, or nothing was prescribed.</p>
          ) : (
            <div className="table-wrap"><table className="data">
              <thead><tr><th>Source</th><th>Medicine</th><th>Decision</th><th>Suggested</th><th>Prescribed</th></tr></thead>
              <tbody>
                {d.decisions.map((x, i) => (
                  <tr key={i}>
                    <td className="small">{SOURCE_LABEL[x.source] ?? x.source}</td>
                    <td>{x.finalItem?.name ?? x.medicineName ?? x.generic}</td>
                    <td><span className={`badge ${DECISION_CLASS[x.decision]}`}>{humanize(x.decision)}</span></td>
                    <td className="small">{x.suggested ? [x.suggested.dose, x.suggested.frequency, x.suggested.durationDays ? `${x.suggested.durationDays} d` : ""].filter(Boolean).join(", ") || "—" : "—"}</td>
                    <td className="small">{x.finalItem ? [x.finalItem.dose, x.finalItem.frequency, x.finalItem.durationDays ? `${x.finalItem.durationDays} d` : ""].filter(Boolean).join(", ") : "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table></div>
          )}
          {d && d.decisions.length > 0 && <p className="small muted">Recorded by {d.decisions[0].decidedBy} at finalization, {dateTime(d.decisions[0].decidedAt)}.</p>}
          {d?.panelsViewed.length ? <p className="small muted">Panels viewed: {d.panelsViewed.map((p) => `${SOURCE_LABEL[p.source]} (${humanize(p.status).toLowerCase()})`).join("; ")}</p> : null}
          {d?.overrides.map((o, i) => (
            <Banner key={i} kind="warn">Safety override by {o.by}, {dateTime(o.at)}: “{o.reason}”<AlertList alerts={o.alerts} /></Banner>
          ))}
        </div>
      </section>
      <section className="panel">
        <div className="panel-head"><h3>Follow-up outcome</h3></div>
        <div className="panel-body stack">
          {d?.outcomes.map((o, i) => (
            <div key={i} className="small" style={{ borderBottom: "1px solid var(--rule)", paddingBottom: 6 }}>
              <span className="badge b-blue">{humanize(o.outcome)}</span> {dateTime(o.recordedAt)} by {o.recordedBy}
              {o.adverseEffects && <div>Adverse effects: {o.adverseEffects}</div>}
              {o.notes && <div className="muted">{o.notes}</div>}
            </div>
          ))}
          {can("clinical.write") ? (
            <>
              {err && <Banner kind="error">{err.message}</Banner>}
              <SelectField label="Outcome at follow-up" value={outcome} onChange={setOutcome} options={["RESOLVED", "IMPROVED", "UNCHANGED", "WORSENED"]} placeholder="Select" />
              <TextArea label="Adverse effects" value={adverse} onChange={setAdverse} rows={2} />
              <TextArea label="Notes" value={notes} onChange={setNotes} rows={2} />
              <Button variant="primary" disabled={!outcome} busy={busy} onClick={record}>Record outcome</Button>
              <p className="small muted" style={{ margin: 0 }}>Outcomes appear in prescribing patterns only as recorded follow-up results; they are not controlled evidence of effectiveness.</p>
            </>
          ) : null}
        </div>
      </section>
    </div>
  );
}
