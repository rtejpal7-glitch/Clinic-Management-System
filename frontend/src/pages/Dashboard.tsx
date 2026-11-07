import { useEffect } from "react";
import { api } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Dashboard, QueueToken, Appointment, Doctor } from "../lib/types";
import { rupees, time, todayIST, weekdayName, dateOnly } from "../lib/format";
import { ErrorBanner, Loading, PageHeader, StatusBadge, useAsync, Empty, Button } from "../components/ui";
import { navigate } from "../lib/router";
import { IconPlus, IconUserPlus } from "../components/icons";

export function DashboardPage() {
  const { user, can } = useAuth();
  const dash = useAsync(() => api.get<Dashboard>("/dashboard/summary"), []);
  const queue = useAsync(() => (can("appointment.read") ? api.get<QueueToken[]>("/queue") : Promise.resolve([])), []);
  const doctors = useAsync(() => (can("schedule.read") ? api.get<Doctor[]>("/doctors") : Promise.resolve([])), []);
  const upcoming = useAsync(
    () => (can("appointment.read") ? api.get<Appointment[]>("/appointments", { date: todayIST(), doctorId: user?.doctorId }) : Promise.resolve([])),
    [],
  );

  useEffect(() => {
    const t = setInterval(() => { dash.reload(); queue.reload(); }, 30000);
    return () => clearInterval(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  if (dash.loading && !dash.data) return <Loading />;
  const d = dash.data;
  const greeting = new Date().getHours() < 12 ? "Good morning" : new Date().getHours() < 17 ? "Good afternoon" : "Good evening";
  const next = (upcoming.data ?? []).filter((a) => a.status === "SCHEDULED" || a.status === "CHECKED_IN").slice(0, 8);

  return (
    <>
      <PageHeader
        title={`${greeting}, ${user!.fullName.split(" ")[0]}`}
        subtitle={`${weekdayName(todayIST(), "long")}, ${dateOnly(todayIST())}`}
        actions={
          <>
            {can("patient.create") && <Button onClick={() => navigate("/patients/new")}><IconUserPlus /> Register patient</Button>}
            {can("appointment.manage") && <Button variant="primary" onClick={() => navigate("/appointments?book=1")}><IconPlus /> Book appointment</Button>}
          </>
        }
      />
      <ErrorBanner error={dash.error} />
      {d && (
        <div className="metrics" aria-label="Today at a glance">
          <Metric label="Registered patients" value={d.totalPatients.toLocaleString("en-IN")} sub={`${d.newPatientsToday} new today`} />
          <Metric label="Today's appointments" value={d.appointmentsToday} sub={`${d.appointmentsByStatus.SCHEDULED ?? 0} yet to arrive`} />
          <Metric label="Waiting now" value={d.waitingNow} sub={`${d.inConsultationNow} in consultation`} />
          <Metric label="Consultations completed" value={d.consultationsCompletedToday} sub="finalized today" />
          {d.pendingPaymentsPaise !== undefined && <Metric label="Pending payments" value={rupees(d.pendingPaymentsPaise, { decimals: false })} sub={`${d.pendingPaymentsCount} open bills`} />}
          {d.revenueTodayPaise !== undefined && <Metric label="Collected today" value={rupees(d.revenueTodayPaise, { decimals: false })} sub="net of refunds" />}
        </div>
      )}

      {(doctors.data?.length ?? 0) > 0 && (
        <>
          <div className="section-title"><h2>Now serving</h2><a href="#/queue" className="small">Open waiting queue</a></div>
          <NowServing doctors={doctors.data!} tokens={queue.data ?? []} />
        </>
      )}

      <div className="split">
        <section>
          <div className="section-title"><h2>{user?.doctorId ? "Your appointments today" : "Upcoming today"}</h2><a className="small" href="#/appointments">All appointments</a></div>
          <div className="table-wrap">
            {next.length === 0 ? (
              <Empty title="Nothing waiting">No scheduled or checked-in patients for the rest of today.</Empty>
            ) : (
              <table className="data">
                <thead><tr><th>Time</th><th>Patient</th><th>Doctor</th><th>Status</th></tr></thead>
                <tbody>
                  {next.map((a) => (
                    <tr key={a.id} className="clickable" onClick={() => navigate(`/patients/${a.patientId}`)}>
                      <td className="num nowrap">{a.appointmentType === "WALK_IN" ? "Walk-in" : time(a.startAt)}</td>
                      <td><div className="cell-main">{a.patientName}</div><div className="cell-sub">{a.patientNumber}</div></td>
                      <td>{a.doctorName}</td>
                      <td><StatusBadge status={a.status} /></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        </section>
        {d?.revenueLast7DaysPaise && (
          <section>
            <div className="section-title"><h2>Collections, last 7 days</h2></div>
            <div className="panel"><div className="panel-body"><RevenueBars days={d.last7Days!} values={d.revenueLast7DaysPaise} /></div></div>
          </section>
        )}
      </div>
    </>
  );
}

function Metric({ label, value, sub }: { label: string; value: React.ReactNode; sub?: string }) {
  return (
    <div className="metric">
      <div className="metric-label">{label}</div>
      <div className="metric-value">{value}</div>
      {sub && <div className="metric-sub">{sub}</div>}
    </div>
  );
}

export function NowServing({ doctors, tokens }: { doctors: Doctor[]; tokens: QueueToken[] }) {
  return (
    <div className="token-board">
      {doctors.map((doc) => {
        const mine = tokens.filter((t) => t.doctorId === doc.id);
        const current = mine.find((t) => t.status === "IN_CONSULTATION") ?? mine.find((t) => t.status === "CALLED");
        const waiting = mine.filter((t) => t.status === "WAITING").length;
        return (
          <div className="now-serving" key={doc.id} aria-label={`${doc.displayName}: ${current ? "now serving token " + current.tokenNumber : "no patient in room"}`}>
            <div className="doc">{doc.displayName}</div>
            <div className={`tok ${current ? "" : "idle"}`}>{current ? String(current.tokenNumber).padStart(3, "0") : "—"}</div>
            <div className="who">{current ? current.patientName : "Room free"}</div>
            <div className="state">{current ? (current.status === "CALLED" ? "Called to room, " : "In consultation, ") : ""}{waiting} waiting</div>
          </div>
        );
      })}
    </div>
  );
}

function RevenueBars({ days, values }: { days: string[]; values: number[] }) {
  const max = Math.max(1, ...values);
  const today = todayIST();
  return (
    <div>
      <div className="bars" role="img" aria-label={`Collections: ${days.map((d, i) => `${weekdayName(d)} ${rupees(values[i], { decimals: false })}`).join(", ")}`}>
        {days.map((d, i) => (
          <div className={`bar-col ${d === today ? "today" : ""}`} key={d} title={`${dateOnly(d)}: ${rupees(values[i])}`}>
            <div className="bar" style={{ height: `${(values[i] / max) * 100}%` }} />
            <span className="bar-label">{weekdayName(d)}</span>
          </div>
        ))}
      </div>
      <p className="small muted" style={{ marginTop: 10, marginBottom: 0 }}>Week total {rupees(values.reduce((a, b) => a + b, 0), { decimals: false })}</p>
    </div>
  );
}
