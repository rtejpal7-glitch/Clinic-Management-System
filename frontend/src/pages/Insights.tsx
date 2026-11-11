// Doctor dashboard: the doctor's own day, follow-ups and prescribing analytics, kept
// separate from the de-identified ClinicFlow network view.
import { useState } from "react";
import { api } from "../lib/api";
import { useAuth } from "../lib/auth";
import { dateOnly, dateTime, humanize, time } from "../lib/format";
import { SOURCE_LABEL } from "../lib/derm";
import { Banner, Empty, ErrorBanner, Loading, PageHeader, StatusBadge, Tabs, useAsync } from "../components/ui";
import { navigate } from "../lib/router";

interface DoctorAnalytics {
  today: { appointments: number; pending: number; inConsultation: number; completed: number; noShow: number };
  todayAppointments: { appointmentId: string; startAt: string; status: string; type: string; patientName: string; patientNumber: string; consultationId?: string | null }[];
  followUpsDue: { consultationId: string; followUpDate: string; patientName: string; patientNumber: string; outcomeRecorded?: string | null }[];
  recentPrescriptions: { consultationId: string; finalizedAt: string; patientNumber: string; diagnosis: string; medicines: number }[];
  topMedicines90d: { name: string; count: number }[];
  topDiagnoses90d: { diagnosis: string; count: number }[];
  monthly: { month: string; consultations: number; dermCases: number }[];
  outcomes: { cases: number; withOutcome: number; improvedOrResolved: number; unchanged: number; worsened: number };
  suggestionUse90d: { source: string; decision: string; count: number }[];
  scope: string;
}
interface NetworkAnalytics {
  status: string; message?: string; note?: string; suppressedDiagnoses?: number;
  thresholds?: Record<string, number>;
  diagnoses: { code: string; display: string; cases: number; doctors: number; clinics: number; casesWithOutcome: number; period: { from: string; to: string } }[];
}

function Bars({ rows }: { rows: { label: string; value: number }[] }) {
  const max = Math.max(1, ...rows.map((r) => r.value));
  if (!rows.length) return <p className="small muted" style={{ margin: 0 }}>No data yet.</p>;
  return (
    <div className="stack" style={{ gap: 6 }}>
      {rows.map((r) => (
        <div key={r.label} className="bar-row">
          <div><div className="small">{r.label}</div><div className="hbar" style={{ ["--accent" as string]: "var(--blue)" }}><span style={{ width: `${(100 * r.value) / max}%` }} /></div></div>
          <span className="tabular small right">{r.value}</span>
        </div>
      ))}
    </div>
  );
}

export function InsightsPage({ tab: initial }: { tab?: string }) {
  const { can, user } = useAuth();
  const [tab, setTab] = useState<"mine" | "network">(initial === "network" ? "network" : "mine");
  const tabs = [
    ...(can("insights.read") && user?.doctorId ? [{ id: "mine" as const, label: "My practice" }] : []),
    ...(can("network.read") ? [{ id: "network" as const, label: "ClinicFlow network" }] : []),
  ];
  if (!tabs.length) return <Empty title="No insights available">Insights are available to users with a doctor profile.</Empty>;
  const active = tabs.some((t) => t.id === tab) ? tab : tabs[0].id;
  return (
    <>
      <PageHeader title="Practice insights" subtitle="Your own work is shown separately from aggregate, de-identified network information" />
      <Tabs tabs={tabs} value={active} onChange={setTab} />
      {active === "mine" ? <MyPractice /> : <Network />}
    </>
  );
}

function MyPractice() {
  const res = useAsync(() => api.get<DoctorAnalytics>("/analytics/doctor"), []);
  if (res.loading && !res.data) return <Loading />;
  if (res.error) return <ErrorBanner error={res.error} />;
  const d = res.data!;
  const use = (src: string) => d.suggestionUse90d.filter((x) => x.source === src);
  return (
    <div className="stack">
      <div className="metrics">
        {[["Today's appointments", d.today.appointments], ["Waiting / scheduled", d.today.pending], ["In consultation", d.today.inConsultation],
          ["Completed today", d.today.completed], ["Follow-ups due (±2 wks)", d.followUpsDue.length]].map(([l, v]) => (
          <div className="metric" key={l as string}><div className="metric-label">{l}</div><div className="metric-value tabular">{v}</div></div>
        ))}
      </div>
      <div className="grid g2">
        <section className="panel">
          <div className="panel-head"><h3>Today</h3></div>
          <div className="panel-body scroll-panel">
            {d.todayAppointments.length === 0 ? <p className="small muted" style={{ margin: 0 }}>No appointments today.</p> : (
              <div className="table-wrap"><table className="data"><tbody>
                {d.todayAppointments.map((a) => (
                  <tr key={a.appointmentId} className={a.consultationId ? "clickable" : ""} onClick={() => a.consultationId && navigate(`/consultations/${a.consultationId}`)}>
                    <td className="tabular">{time(a.startAt)}</td><td><div className="cell-main">{a.patientName}</div><div className="cell-sub">{a.patientNumber}</div></td>
                    <td><StatusBadge status={a.type} /></td><td><StatusBadge status={a.status} /></td>
                  </tr>
                ))}
              </tbody></table></div>
            )}
          </div>
        </section>
        <section className="panel">
          <div className="panel-head"><h3>Follow-ups due</h3><span className="small muted">record outcomes from the consultation</span></div>
          <div className="panel-body scroll-panel">
            {d.followUpsDue.length === 0 ? <p className="small muted" style={{ margin: 0 }}>No follow-ups due in the next two weeks.</p> : (
              <div className="table-wrap"><table className="data"><tbody>
                {d.followUpsDue.map((f) => (
                  <tr key={f.consultationId} className="clickable" onClick={() => navigate(`/consultations/${f.consultationId}`)}>
                    <td className="tabular">{dateOnly(f.followUpDate)}</td><td><div className="cell-main">{f.patientName}</div><div className="cell-sub">{f.patientNumber}</div></td>
                    <td>{f.outcomeRecorded ? <StatusBadge status="DONE" label={humanize(f.outcomeRecorded)} /> : <span className="small muted">Outcome not recorded</span>}</td>
                  </tr>
                ))}
              </tbody></table></div>
            )}
          </div>
        </section>
        <section className="panel">
          <div className="panel-head"><h3>Frequently prescribed (90 days)</h3></div>
          <div className="panel-body"><Bars rows={d.topMedicines90d.map((m) => ({ label: m.name, value: m.count }))} /></div>
        </section>
        <section className="panel">
          <div className="panel-head"><h3>Common diagnoses (90 days)</h3></div>
          <div className="panel-body"><Bars rows={d.topDiagnoses90d.map((m) => ({ label: m.diagnosis, value: m.count }))} /></div>
        </section>
        <section className="panel">
          <div className="panel-head"><h3>Consultations per month</h3></div>
          <div className="panel-body"><Bars rows={d.monthly.map((m) => ({ label: m.month, value: m.consultations }))} /></div>
        </section>
        <section className="panel">
          <div className="panel-head"><h3>Recorded follow-up outcomes</h3></div>
          <div className="panel-body small stack" style={{ gap: 6 }}>
            <div>{d.outcomes.withOutcome} of {d.outcomes.cases} dermatology cases have a recorded outcome.</div>
            {d.outcomes.withOutcome > 0 && <div>Resolved/improved {d.outcomes.improvedOrResolved} · unchanged {d.outcomes.unchanged} · worsened {d.outcomes.worsened}</div>}
            <div className="muted">Outcomes are only those recorded at follow-up; they are not controlled comparisons of treatment effectiveness.</div>
            <h4 style={{ marginTop: 8 }}>How you used decision support (90 days)</h4>
            {["CLINICAL", "MY_PATTERNS", "NETWORK", "DOCTOR"].map((s) => use(s).length ? (
              <div key={s}><strong>{SOURCE_LABEL[s]}:</strong> {use(s).map((x) => `${humanize(x.decision).toLowerCase()} ${x.count}`).join(", ")}</div>
            ) : null)}
          </div>
        </section>
      </div>
      <section className="panel">
        <div className="panel-head"><h3>Recent prescriptions</h3></div>
        <div className="panel-body">
          <div className="table-wrap"><table className="data">
            <thead><tr><th>Finalized</th><th>Patient no.</th><th>Diagnosis</th><th className="num">Medicines</th></tr></thead>
            <tbody>
              {d.recentPrescriptions.map((r) => (
                <tr key={r.consultationId} className="clickable" onClick={() => navigate(`/consultations/${r.consultationId}`)}>
                  <td>{dateTime(r.finalizedAt)}</td><td className="tabular">{r.patientNumber}</td><td>{r.diagnosis}</td><td className="num">{r.medicines}</td>
                </tr>
              ))}
            </tbody>
          </table></div>
        </div>
      </section>
      <p className="small muted">{d.scope}</p>
    </div>
  );
}

function Network() {
  const res = useAsync(() => api.get<NetworkAnalytics>("/analytics/network"), []);
  if (res.loading && !res.data) return <Loading />;
  if (res.error) return <ErrorBanner error={res.error} />;
  const d = res.data!;
  if (d.status === "NOT_PARTICIPATING") return <Banner kind="info">{d.message} An administrator can join from Clinic network settings.</Banner>;
  return (
    <div className="stack">
      <Banner kind="info">{d.note} Network data comes only from clinics that joined under a data-sharing agreement and is not representative of all dermatologists. Prescribing frequency does not show effectiveness.</Banner>
      <section className="panel">
        <div className="panel-head"><h3>Diagnoses with enough network data</h3><span className="small muted">{d.suppressedDiagnoses} below threshold, not shown</span></div>
        <div className="panel-body">
          {d.diagnoses.length === 0 ? <p className="small muted" style={{ margin: 0 }}>No diagnosis meets the disclosure thresholds yet.</p> : (
            <div className="table-wrap"><table className="data">
              <thead><tr><th>Diagnosis</th><th className="num">Cases</th><th className="num">Doctors</th><th className="num">Clinics</th><th className="num">With outcome</th><th>Period</th></tr></thead>
              <tbody>
                {d.diagnoses.map((x) => (
                  <tr key={x.code}><td><span className="chip tabular">{x.code}</span> {x.display}</td><td className="num">{x.cases}</td><td className="num">{x.doctors}</td>
                    <td className="num">{x.clinics}</td><td className="num">{x.casesWithOutcome}</td><td className="small">{x.period.from} to {x.period.to}</td></tr>
                ))}
              </tbody>
            </table></div>
          )}
          {d.thresholds && <p className="small muted">Thresholds: at least {d.thresholds.minCases} cases, {d.thresholds.minDoctors} doctors and {d.thresholds.minClinics} clinics; no clinic above {d.thresholds.maxShareFromOneClinicPercent}% of cases.</p>}
        </div>
      </section>
    </div>
  );
}
