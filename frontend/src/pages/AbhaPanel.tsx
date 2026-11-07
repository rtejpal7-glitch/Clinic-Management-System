import { useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { AbdmTxn, Consent, Identifier, Patient } from "../lib/types";
import { dateOnly, dateTime, formatAbha, humanize } from "../lib/format";
import { Banner, Button, Empty, Loading, MockFlag, Modal, SelectField, StatusBadge, TextField, useAsync, useToast } from "../components/ui";

const PURPOSES: Record<string, string> = {
  CAREMGT: "Care management", BTG: "Break the glass (emergency)", PUBHLTH: "Public health", HPAYMT: "Healthcare payment",
  DSRCH: "Disease-specific research", PATRQT: "Self requested by patient",
};
const HI_TYPES = ["OPConsultation", "Prescription", "DiagnosticReport", "DischargeSummary", "ImmunizationRecord", "HealthDocumentRecord", "WellnessRecord"];

export function AbhaPanel({ patient, onChanged }: { patient: Patient; onChanged: () => void }) {
  const { can } = useAuth();
  const toast = useToast();
  const ids = useAsync(() => api.get<Identifier[]>(`/patients/${patient.id}/abha`), [patient.id]);
  const consents = useAsync(() => api.get<Consent[]>(`/patients/${patient.id}/consents`), [patient.id]);
  const txns = useAsync(() => api.get<AbdmTxn[]>("/abdm/transactions", { patientId: patient.id }), [patient.id]);
  const [linking, setLinking] = useState(false);
  const [requesting, setRequesting] = useState(false);
  const refresh = () => { ids.reload(); consents.reload(); txns.reload(); onChanged(); };
  const verified = (ids.data ?? []).some((i) => i.verificationStatus === "VERIFIED");

  const act = async (fn: () => Promise<unknown>, ok: string) => {
    try { await fn(); toast(ok); refresh(); } catch (e) { toast((e as ApiError).message, "error"); }
  };

  return (
    <div className="stack">
      <Banner kind="mock">
        <strong>ABDM is running in mock mode.</strong> Nothing on this tab is sent to or received from ABDM. ABHA verification, consent
        responses and health-record sharing are simulated so the workflow can be demonstrated. A live integration needs ABDM onboarding and credentials.
      </Banner>

      <section>
        <div className="section-title">
          <h2>ABHA identifiers</h2>
          {can("abdm.link") && <Button variant="primary" size="sm" onClick={() => setLinking(true)}>Link ABHA</Button>}
        </div>
        <p className="small muted">The clinic's patient ID ({patient.patientNumber}) stays the primary identifier. An ABHA is linked alongside it only after the patient authenticates with an OTP.</p>
        <div className="table-wrap">
          {ids.loading ? <Loading /> : !ids.data?.length ? <Empty title="No ABHA linked">Ask the patient for their ABHA number or ABHA address.</Empty> : (
            <table className="data">
              <thead><tr><th>Type</th><th>Value</th><th>Status</th><th>Verified</th><th>Source</th><th /></tr></thead>
              <tbody>
                {ids.data.map((i) => (
                  <tr key={i.id}>
                    <td>{humanize(i.idType)}</td>
                    <td className="num">{i.valueMasked}</td>
                    <td><StatusBadge status={i.verificationStatus} /></td>
                    <td className="nowrap">{i.verifiedAt ? `${dateTime(i.verifiedAt)} via ${humanize(i.verificationMethod)}` : "—"}</td>
                    <td>{i.isMock ? <MockFlag>Simulated</MockFlag> : "ABDM"}</td>
                    <td className="right">
                      {can("abdm.link") && i.verificationStatus !== "UNLINKED" && (
                        <Button size="sm" variant="ghost" onClick={() => {
                          const reason = window.prompt("Reason for unlinking this ABHA?");
                          if (reason) act(() => api.post(`/patients/${patient.id}/abha/${i.id}/unlink`, { reason }), "ABHA unlinked");
                        }}>Unlink</Button>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </section>

      <section>
        <div className="section-title">
          <h2>Consent</h2>
          {can("abdm.consent") && <Button size="sm" onClick={() => setRequesting(true)} disabled={!verified} title={verified ? "" : "Link and verify an ABHA first"}>Request consent</Button>}
        </div>
        {!verified && <p className="small muted">Consent requests need a verified ABHA link.</p>}
        <div className="table-wrap">
          {consents.loading ? <Loading /> : !consents.data?.length ? <Empty title="No consent requests" /> : (
            <table className="data">
              <thead><tr><th>Purpose</th><th>Records</th><th>Period</th><th>Expires</th><th>Status</th><th /></tr></thead>
              <tbody>
                {consents.data.map((c) => (
                  <tr key={c.id}>
                    <td><div className="cell-main">{PURPOSES[c.purposeCode] ?? c.purposeCode}</div><div className="cell-sub">{c.direction === "HIP_SHARE" ? "Clinic shares records" : "Clinic requests records"}</div></td>
                    <td className="small">{c.hiTypes.join(", ")}</td>
                    <td className="nowrap small">{dateOnly(c.dateFrom)} to {dateOnly(c.dateTo)}</td>
                    <td className="nowrap small">{dateTime(c.expiresAt)}</td>
                    <td><StatusBadge status={c.status} />{c.statusReason && <div className="cell-sub">{c.statusReason}</div>}</td>
                    <td className="right nowrap">
                      {c.status === "REQUESTED" && c.consentRequestRef && can("abdm.consent") && (
                        <>
                          <Button size="sm" variant="ghost" onClick={() => act(() => api.post(`/consents/${c.id}/mock-respond`, { decision: "GRANT" }), "Simulated: patient granted consent")}>Simulate grant</Button>
                          <Button size="sm" variant="ghost" onClick={() => act(() => api.post(`/consents/${c.id}/mock-respond`, { decision: "DENY" }), "Simulated: patient denied consent")}>Simulate deny</Button>
                        </>
                      )}
                      {(c.status === "GRANTED" || c.status === "REQUESTED") && can("abdm.consent") && (
                        <Button size="sm" variant="danger" onClick={() => {
                          const reason = window.prompt("Reason for withdrawing consent?");
                          if (reason) act(() => api.post(`/consents/${c.id}/revoke`, { reason }), "Consent withdrawn");
                        }}>Withdraw</Button>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </section>

      <section>
        <div className="section-title"><h2>ABDM transaction log</h2></div>
        <div className="table-wrap">
          {txns.loading ? <Loading /> : !txns.data?.length ? <Empty title="No transactions yet" /> : (
            <table className="data">
              <thead><tr><th>When</th><th>Operation</th><th>Status</th><th>Attempts</th><th>Details</th><th /></tr></thead>
              <tbody>
                {txns.data.map((t) => (
                  <tr key={t.id}>
                    <td className="nowrap">{dateTime(t.createdAt)}</td>
                    <td>{humanize(t.txnType)} {t.isMock && <MockFlag>Mock</MockFlag>}</td>
                    <td><StatusBadge status={t.status} /></td>
                    <td className="num">{t.attemptCount} / {t.maxAttempts}</td>
                    <td className="small">{t.errorMessage ?? (t.gatewayTxnId ? `Ref ${t.gatewayTxnId}` : "—")}</td>
                    <td className="right">
                      {(t.status === "RETRY_SCHEDULED" || t.status === "FAILED") && ["HI_SHARE", "CONSENT_REQUEST"].includes(t.txnType) && t.attemptCount < t.maxAttempts && can("abdm.consent") && (
                        <Button size="sm" onClick={() => act(() => api.post(`/abdm/transactions/${t.id}/retry`), "Retried")}>Retry now</Button>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </section>

      {linking && <LinkAbhaModal patient={patient} onClose={() => setLinking(false)} onDone={() => { setLinking(false); refresh(); }} />}
      {requesting && <ConsentModal patientId={patient.id} onClose={() => setRequesting(false)} onDone={() => { setRequesting(false); refresh(); }} />}
    </div>
  );
}

function LinkAbhaModal({ patient, onClose, onDone }: { patient: Patient; onClose: () => void; onDone: () => void }) {
  const toast = useToast();
  const [step, setStep] = useState<"id" | "otp">("id");
  const [idType, setIdType] = useState("ABHA_NUMBER");
  const [value, setValue] = useState("");
  const [method, setMethod] = useState("MOBILE_OTP");
  const [consented, setConsented] = useState(false);
  const [otp, setOtp] = useState("");
  const [txn, setTxn] = useState<AbdmTxn | null>(null);
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [message, setMessage] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const start = async () => {
    setBusy(true); setErrors({}); setMessage(null);
    try {
      const t = await api.post<AbdmTxn>(`/patients/${patient.id}/abha/verify-init`, { idType, value, authMethod: method, patientConsented: consented });
      if (t.status !== "SUCCESS") { setMessage(t.errorMessage ?? "Could not start verification"); return; }
      setTxn(t); setStep("otp");
    } catch (e) {
      const err = e as ApiError;
      setErrors(err.fields); setMessage(Object.keys(err.fields).length ? null : err.message);
    } finally { setBusy(false); }
  };
  const confirm = async () => {
    setBusy(true); setMessage(null);
    try {
      const t = await api.post<AbdmTxn>(`/abdm/transactions/${txn!.id}/verify-confirm`, { otp });
      if (t.status === "SUCCESS") { toast("ABHA linked (simulated verification)"); onDone(); }
      else setMessage(t.errorMessage ?? "Verification failed");
    } catch (e) { setMessage((e as ApiError).message); } finally { setBusy(false); }
  };

  return (
    <Modal title="Link ABHA" onClose={onClose}
      footer={step === "id"
        ? <><Button onClick={onClose}>Cancel</Button><Button variant="primary" busy={busy} onClick={start}>Send OTP to patient</Button></>
        : <><Button onClick={() => setStep("id")}>Back</Button><Button variant="primary" busy={busy} disabled={otp.length !== 6} onClick={confirm}>Verify and link</Button></>}>
      <Banner kind="mock">Mock mode: no OTP is actually sent. Enter <strong>123456</strong> to simulate success, <strong>999999</strong> to simulate a gateway timeout, anything else to simulate a wrong OTP.</Banner>
      {message && <Banner kind="error">{message}</Banner>}
      {step === "id" ? (
        <div className="stack">
          <SelectField label="Identifier type" value={idType} onChange={(v) => { setIdType(v); setValue(""); }} options={[{ value: "ABHA_NUMBER", label: "ABHA number (14 digits)" }, { value: "ABHA_ADDRESS", label: "ABHA address (name@abdm)" }]} />
          <TextField label={idType === "ABHA_NUMBER" ? "ABHA number" : "ABHA address"} value={value} error={errors.value}
            onChange={(v) => setValue(idType === "ABHA_NUMBER" ? formatAbha(v) : v.toLowerCase().trim())}
            placeholder={idType === "ABHA_NUMBER" ? "XX-XXXX-XXXX-XXXX" : "name@abdm"} inputMode={idType === "ABHA_NUMBER" ? "numeric" : "text"} />
          <SelectField label="Authenticate with" value={method} onChange={setMethod} options={[{ value: "MOBILE_OTP", label: "OTP to ABHA-linked mobile" }, { value: "AADHAAR_OTP", label: "Aadhaar OTP" }]} />
          <label className="check">
            <input type="checkbox" checked={consented} onChange={(e) => setConsented(e.target.checked)} />
            <span>The patient has agreed to link their ABHA to this clinic's record and will share the OTP they receive.</span>
          </label>
          {errors.patientConsented && <span className="err small" style={{ color: "var(--red)" }}>{errors.patientConsented}</span>}
        </div>
      ) : (
        <div className="stack">
          <p className="small">Ask {patient.fullName} for the 6-digit OTP sent to them.</p>
          <TextField label="OTP" value={otp} onChange={(v) => setOtp(v.replace(/\D/g, "").slice(0, 6))} inputMode="numeric" autoFocus autoComplete="one-time-code" />
        </div>
      )}
    </Modal>
  );
}

function ConsentModal({ patientId, onClose, onDone }: { patientId: string; onClose: () => void; onDone: () => void }) {
  const toast = useToast();
  const today = new Date().toISOString().slice(0, 10);
  const [purpose, setPurpose] = useState("CAREMGT");
  const [direction, setDirection] = useState("HIP_SHARE");
  const [types, setTypes] = useState<string[]>(["OPConsultation", "Prescription"]);
  const [from, setFrom] = useState(new Date(Date.now() - 365 * 864e5).toISOString().slice(0, 10));
  const [to, setTo] = useState(today);
  const [expiry, setExpiry] = useState(new Date(Date.now() + 30 * 864e5).toISOString().slice(0, 10));
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [msg, setMsg] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const submit = async () => {
    setBusy(true); setErrors({}); setMsg(null);
    try {
      const c = await api.post<Consent>(`/patients/${patientId}/consents`, {
        direction, purposeCode: purpose, hiTypes: types, dateFrom: from, dateTo: to, expiresAt: new Date(expiry + "T23:59:00+05:30").toISOString(),
      });
      toast(c.consentRequestRef ? "Consent request sent (mock)" : "Consent request saved; the gateway call will be retried");
      onDone();
    } catch (e) { const err = e as ApiError; setErrors(err.fields); setMsg(err.message); } finally { setBusy(false); }
  };
  return (
    <Modal title="Request consent" onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" busy={busy} disabled={!types.length} onClick={submit}>Send consent request</Button></>}>
      {msg && <Banner kind="error">{msg}</Banner>}
      <div className="grid g2">
        <SelectField label="Purpose" value={purpose} onChange={setPurpose} options={Object.entries(PURPOSES).map(([value, label]) => ({ value, label }))} className="span-all" />
        <SelectField label="Direction" value={direction} onChange={setDirection} className="span-all"
          options={[{ value: "HIP_SHARE", label: "Share this clinic's records with the patient's chosen recipient" }, { value: "HIU_REQUEST", label: "Request records held by other facilities" }]} />
        <fieldset className="fieldset span-all" style={{ marginBottom: 0 }}>
          <legend className="small" style={{ fontWeight: 600, color: "var(--ink-2)" }}>Health information types</legend>
          <div className="grid g2" style={{ gap: 6 }}>
            {HI_TYPES.map((t) => (
              <label key={t} className="check"><input type="checkbox" checked={types.includes(t)} onChange={(e) => setTypes(e.target.checked ? [...types, t] : types.filter((x) => x !== t))} />{t}</label>
            ))}
          </div>
          {errors.hiTypes && <span className="err">{errors.hiTypes}</span>}
        </fieldset>
        <TextField label="Records from" type="date" value={from} onChange={setFrom} error={errors.dateFrom} />
        <TextField label="Records to" type="date" value={to} onChange={setTo} error={errors.dateTo} />
        <TextField label="Consent expires on" type="date" value={expiry} onChange={setExpiry} error={errors.expiresAt} hint="Within one year" />
      </div>
    </Modal>
  );
}
