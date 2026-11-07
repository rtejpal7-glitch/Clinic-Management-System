import { useState } from "react";
import { api, ApiError } from "../lib/api";
import type { AuditLog, Page } from "../lib/types";
import { dateTime, humanize, todayIST, addDays } from "../lib/format";
import { Banner, Button, Empty, ErrorBanner, Loading, Modal, PageHeader, useAsync, useToast } from "../components/ui";
import { IconShield } from "../components/icons";

const ACTIONS = ["AUTH_LOGIN", "AUTH_LOGOUT", "ACCESS_DENIED", "PATIENT_CREATE", "PATIENT_UPDATE", "PATIENT_VIEW", "PATIENT_SEARCH", "PATIENT_DUPLICATE_OVERRIDE",
  "PATIENT_CLINICAL_HISTORY_VIEW", "APPOINTMENT_CREATE", "APPOINTMENT_CANCEL", "APPOINTMENT_RESCHEDULE", "APPOINTMENT_CHECK_IN", "CONSULTATION_START",
  "CONSULTATION_VIEW", "CONSULTATION_UPDATE", "CONSULTATION_FINALIZE", "CONSULTATION_ADDENDUM", "PRESCRIPTION_UPDATE", "BILL_CREATE", "PAYMENT_RECORD",
  "PAYMENT_REFUND", "BILL_CANCEL", "USER_CREATE", "USER_UPDATE", "USER_PASSWORD_RESET", "ROLE_PERMISSIONS_UPDATE", "SETTINGS_UPDATE", "ABHA_LINK_INIT",
  "ABHA_LINK_CONFIRM", "CONSENT_REQUEST", "CONSENT_REVOKE", "HI_SHARE", "FHIR_EXPORT"];

export function AuditPage() {
  const toast = useToast();
  const [from, setFrom] = useState(addDays(todayIST(), -7));
  const [to, setTo] = useState(todayIST());
  const [action, setAction] = useState("");
  const [outcome, setOutcome] = useState("");
  const [page, setPage] = useState(1);
  const [detail, setDetail] = useState<AuditLog | null>(null);
  const [verify, setVerify] = useState<{ valid: boolean; checkedRecords: number; firstInvalidId?: number } | null>(null);
  const logs = useAsync(() => api.get<Page<AuditLog>>("/audit-logs", {
    from: new Date(from + "T00:00:00+05:30").toISOString(), to: new Date(addDays(to, 1) + "T00:00:00+05:30").toISOString(), action, outcome, page, size: 50,
  }), [from, to, action, outcome, page]);
  const pages = logs.data ? Math.max(1, Math.ceil(logs.data.total / 50)) : 1;

  return (
    <>
      <PageHeader title="Audit log" subtitle="Append-only record of sign-ins, record access and every change. Entries cannot be edited or deleted from the application."
        actions={<Button onClick={async () => { try { setVerify(await api.get("/audit-logs/verify")); } catch (e) { toast((e as ApiError).message, "error"); } }}><IconShield /> Verify integrity</Button>} />
      {verify && (verify.valid
        ? <Banner kind="info">Integrity check passed: all {verify.checkedRecords.toLocaleString("en-IN")} entries match their hash chain.</Banner>
        : <Banner kind="error">Integrity check failed at entry #{verify.firstInvalidId}. The log may have been altered outside the application. Escalate to the system administrator.</Banner>)}
      <div className="row" style={{ marginBottom: 14 }}>
        <label className="small">From <input type="date" className="input" style={{ width: 150, display: "inline-block" }} value={from} onChange={(e) => { setFrom(e.target.value); setPage(1); }} /></label>
        <label className="small">To <input type="date" className="input" style={{ width: 150, display: "inline-block" }} value={to} onChange={(e) => { setTo(e.target.value); setPage(1); }} /></label>
        <select className="select" style={{ width: 260 }} value={action} onChange={(e) => { setAction(e.target.value); setPage(1); }} aria-label="Action">
          <option value="">All actions</option>{ACTIONS.map((a) => <option key={a} value={a}>{humanize(a)}</option>)}
        </select>
        <select className="select" style={{ width: 150 }} value={outcome} onChange={(e) => { setOutcome(e.target.value); setPage(1); }} aria-label="Outcome">
          <option value="">Any outcome</option><option value="SUCCESS">Success</option><option value="FAILURE">Failure</option><option value="DENIED">Denied</option>
        </select>
        {logs.data && <span className="small muted">{logs.data.total.toLocaleString("en-IN")} entries</span>}
      </div>
      <ErrorBanner error={logs.error} />
      <div className="table-wrap">
        {logs.loading && !logs.data ? <Loading /> : !logs.data?.items.length ? <Empty title="No entries for these filters" /> : (
          <table className="data">
            <thead><tr><th>#</th><th>When</th><th>User</th><th>Action</th><th>Record</th><th>Outcome</th><th>IP</th></tr></thead>
            <tbody>
              {logs.data.items.map((l) => (
                <tr key={l.id} className="clickable" onClick={() => setDetail(l)}>
                  <td className="num muted">{l.id}</td>
                  <td className="nowrap small">{dateTime(l.occurredAt)}</td>
                  <td><div className="cell-main">{l.username ?? "—"}</div><div className="cell-sub">{l.roles ? humanize(l.roles) : ""}</div></td>
                  <td>{humanize(l.action)}</td>
                  <td className="small">{l.entityType ? humanize(l.entityType) : ""}{l.patientId && <div className="cell-sub"><a href={`#/patients/${l.patientId}`} onClick={(e) => e.stopPropagation()}>Patient record</a></div>}</td>
                  <td><span className={`badge ${l.outcome === "SUCCESS" ? "b-green" : "b-red"}`}>{humanize(l.outcome)}</span></td>
                  <td className="small muted">{l.ipAddress}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      {pages > 1 && (
        <div className="row" style={{ marginTop: 12, justifyContent: "flex-end" }}>
          <Button size="sm" disabled={page <= 1} onClick={() => setPage(page - 1)}>Newer</Button>
          <span className="small muted">Page {page} of {pages}</span>
          <Button size="sm" disabled={page >= pages} onClick={() => setPage(page + 1)}>Older</Button>
        </div>
      )}
      {detail && (
        <Modal title={`Audit entry #${detail.id}`} onClose={() => setDetail(null)}>
          <dl className="kv">
            <dt>When</dt><dd>{dateTime(detail.occurredAt)}</dd>
            <dt>User</dt><dd>{detail.username ?? "—"} {detail.userId && <span className="hash">{detail.userId}</span>}</dd>
            <dt>Action</dt><dd>{humanize(detail.action)}</dd>
            <dt>Outcome</dt><dd>{humanize(detail.outcome)}</dd>
            <dt>Record</dt><dd>{detail.entityType} <span className="hash">{detail.entityId}</span></dd>
            <dt>IP address</dt><dd>{detail.ipAddress}</dd>
            <dt>Chain hash</dt><dd className="hash" style={{ wordBreak: "break-all" }}>{detail.hash}</dd>
          </dl>
          <h3 style={{ margin: "14px 0 6px" }}>Details</h3>
          <pre className="json">{detail.details ? JSON.stringify(JSON.parse(detail.details), null, 2) : "{}"}</pre>
        </Modal>
      )}
    </>
  );
}
