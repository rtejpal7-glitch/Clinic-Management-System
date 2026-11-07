import { useEffect, useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Appointment, Doctor, ScheduleBlock } from "../lib/types";
import { addDays, dateOnly, isoWeekday, time, todayIST, weekdayName } from "../lib/format";
import { navigate } from "../lib/router";
import { Banner, Button, Empty, Loading, Modal, PageHeader, Segmented, StatusBadge, useAsync, useToast } from "../components/ui";
import { IconPlus, IconTrash } from "../components/icons";

const DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

function weekStart(ymd: string) {
  return addDays(ymd, -(isoWeekday(ymd) - 1));
}

export function DoctorSchedulePage() {
  const { can, user } = useAuth();
  const doctors = useAsync(() => api.get<Doctor[]>("/doctors"), []);
  const [doctorId, setDoctorId] = useState(user?.doctorId ?? "");
  const [view, setView] = useState<"day" | "week">("week");
  const [anchor, setAnchor] = useState(todayIST());
  const [editing, setEditing] = useState(false);
  useEffect(() => { if (!doctorId && doctors.data?.length) setDoctorId(doctors.data[0].id); }, [doctors.data, doctorId]);

  const start = view === "week" ? weekStart(anchor) : anchor;
  const end = addDays(start, view === "week" ? 7 : 1);
  const schedule = useAsync(() => (doctorId ? api.get<ScheduleBlock[]>(`/doctors/${doctorId}/schedule`) : Promise.resolve([])), [doctorId]);
  const appts = useAsync(
    () => (doctorId ? api.get<Appointment[]>("/appointments", {
      doctorId, from: new Date(start + "T00:00:00+05:30").toISOString(), to: new Date(end + "T00:00:00+05:30").toISOString(),
    }) : Promise.resolve([])),
    [doctorId, start, end],
  );
  const doctor = doctors.data?.find((d) => d.id === doctorId);

  if (doctors.loading) return <Loading />;
  if (!doctors.data?.length) return <Empty title="No doctors set up">{can("doctor.manage") && <a href="#/settings">Add a doctor profile in clinic settings</a>}</Empty>;

  return (
    <>
      <PageHeader title="Doctor schedule" subtitle={doctor ? `${doctor.displayName}${doctor.specialization ? `, ${doctor.specialization}` : ""}` : undefined}
        actions={can("schedule.manage") && doctorId && <Button onClick={() => setEditing(true)}>Edit working hours</Button>} />
      <div className="row" style={{ marginBottom: 14 }}>
        <select className="select" style={{ width: 240 }} value={doctorId} onChange={(e) => setDoctorId(e.target.value)} aria-label="Doctor">
          {doctors.data.map((d) => <option key={d.id} value={d.id}>{d.displayName}</option>)}
        </select>
        <Segmented label="View" value={view} onChange={setView} options={[{ id: "day", label: "Day" }, { id: "week", label: "Week" }]} />
        <Button size="sm" onClick={() => setAnchor(addDays(anchor, view === "week" ? -7 : -1))}>Previous</Button>
        <Button size="sm" onClick={() => setAnchor(todayIST())}>Today</Button>
        <Button size="sm" onClick={() => setAnchor(addDays(anchor, view === "week" ? 7 : 1))}>Next</Button>
        <span className="small muted">{view === "week" ? `${dateOnly(start)} to ${dateOnly(addDays(end, -1))}` : `${weekdayName(start, "long")}, ${dateOnly(start)}`}</span>
      </div>
      {schedule.data && schedule.data.length === 0 && <Banner kind="warn">No working hours are set for this doctor, so no slots can be booked.</Banner>}
      {view === "week" ? (
        <div className="week">
          {Array.from({ length: 7 }, (_, i) => addDays(start, i)).map((d) => {
            const blocks = (schedule.data ?? []).filter((b) => b.weekday === isoWeekday(d));
            const list = (appts.data ?? []).filter((a) => dateIST(a.startAt) === d);
            return (
              <div key={d} className={`week-day ${d === todayIST() ? "today" : ""}`}>
                <div className="week-day-head"><span>{weekdayName(d)} {d.slice(8)}</span><span className="muted small">{list.filter((a) => a.status !== "CANCELLED").length}</span></div>
                <div className="week-day-body">
                  <div className="small muted">{blocks.length ? blocks.map((b) => `${b.startTime}–${b.endTime}`).join(", ") : "Off"}</div>
                  {list.map((a) => (
                    <div key={a.id} className={`appt-chip ${a.status}`} onClick={() => navigate(`/patients/${a.patientId}`)} title={`${a.patientName}: ${a.status}`}>
                      <span className="tabular">{a.appointmentType === "WALK_IN" ? "Walk-in" : time(a.startAt)}</span> {a.patientName}
                    </div>
                  ))}
                </div>
              </div>
            );
          })}
        </div>
      ) : (
        <DayView date={start} blocks={(schedule.data ?? []).filter((b) => b.weekday === isoWeekday(start))} appts={(appts.data ?? []).filter((a) => dateIST(a.startAt) === start)} />
      )}
      {editing && doctor && <EditHoursModal doctor={doctor} blocks={schedule.data ?? []} onClose={() => setEditing(false)} onSaved={() => { setEditing(false); schedule.reload(); }} />}
    </>
  );
}

const dateIST = (iso: string) => new Intl.DateTimeFormat("en-CA", { timeZone: "Asia/Kolkata" }).format(new Date(iso));
const hourIST = (iso: string) => Number(new Intl.DateTimeFormat("en-GB", { timeZone: "Asia/Kolkata", hour: "2-digit", hour12: false }).format(new Date(iso)));

function DayView({ date, blocks, appts }: { date: string; blocks: ScheduleBlock[]; appts: Appointment[] }) {
  const hours = new Set<number>();
  blocks.forEach((b) => { for (let h = Number(b.startTime.slice(0, 2)); h < Number(b.endTime.slice(0, 2)) + (b.endTime.endsWith("00") ? 0 : 1); h++) hours.add(h); });
  appts.forEach((a) => hours.add(hourIST(a.startAt)));
  const sorted = [...hours].sort((a, b) => a - b);
  if (!sorted.length) return <Empty title={`No working hours on ${weekdayName(date, "long")}`} />;
  return (
    <div className="panel"><div className="panel-body">
      <div className="day-timeline">
        {sorted.map((h) => (
          <div key={h} style={{ display: "contents" }}>
            <div className="day-time">{String(h).padStart(2, "0")}:00</div>
            <div className="day-slot">
              {appts.filter((a) => hourIST(a.startAt) === h).map((a) => (
                <div key={a.id} className={`appt-chip ${a.status}`} onClick={() => navigate(`/patients/${a.patientId}`)}>
                  <div className="row" style={{ justifyContent: "space-between" }}>
                    <span><span className="tabular">{a.appointmentType === "WALK_IN" ? "Walk-in" : time(a.startAt)}</span> <strong>{a.patientName}</strong> {a.reason ? `— ${a.reason}` : ""}</span>
                    <StatusBadge status={a.status} />
                  </div>
                </div>
              ))}
            </div>
          </div>
        ))}
      </div>
    </div></div>
  );
}

function EditHoursModal({ doctor, blocks, onClose, onSaved }: { doctor: Doctor; blocks: ScheduleBlock[]; onClose: () => void; onSaved: () => void }) {
  const toast = useToast();
  const [rows, setRows] = useState<ScheduleBlock[]>(blocks.map(({ weekday, startTime, endTime, slotMinutes }) => ({ weekday, startTime, endTime, slotMinutes })));
  const [err, setErr] = useState<ApiError | null>(null);
  const update = (i: number, patch: Partial<ScheduleBlock>) => setRows(rows.map((r, j) => (j === i ? { ...r, ...patch } : r)));
  const save = async () => {
    try {
      await api.put(`/doctors/${doctor.id}/schedule`, { blocks: rows });
      toast("Working hours saved");
      onSaved();
    } catch (e) { setErr(e as ApiError); }
  };
  const addWeekdays = () => {
    const extra = [1, 2, 3, 4, 5, 6].filter((d) => !rows.some((r) => r.weekday === d)).map((d) => ({ weekday: d, startTime: "09:30", endTime: "13:30", slotMinutes: 15 }));
    setRows([...rows, ...extra].sort((a, b) => a.weekday - b.weekday || a.startTime.localeCompare(b.startTime)));
  };
  return (
    <Modal title={`Working hours: ${doctor.displayName}`} wide onClose={onClose}
      footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" onClick={save}>Save working hours</Button></>}>
      <p className="small muted">Add one row per session (for example a morning and an evening OPD). Existing bookings are not changed.</p>
      {err && <Banner kind="error">{err.message}{Object.values(err.fields).length > 0 && <> — {Object.values(err.fields)[0]}</>}</Banner>}
      <div className="table-wrap" style={{ marginBottom: 12 }}>
        <table className="data">
          <thead><tr><th>Day</th><th>From</th><th>To</th><th>Slot length</th><th /></tr></thead>
          <tbody>
            {rows.map((r, i) => (
              <tr key={i}>
                <td><select className="select" value={r.weekday} onChange={(e) => update(i, { weekday: Number(e.target.value) })} aria-label="Day">{DAYS.map((d, j) => <option key={d} value={j + 1}>{d}</option>)}</select></td>
                <td><input className="input" type="time" value={r.startTime} onChange={(e) => update(i, { startTime: e.target.value })} aria-label="From" /></td>
                <td><input className="input" type="time" value={r.endTime} onChange={(e) => update(i, { endTime: e.target.value })} aria-label="To" /></td>
                <td><select className="select" value={r.slotMinutes} onChange={(e) => update(i, { slotMinutes: Number(e.target.value) })} aria-label="Slot length">{[5, 10, 15, 20, 30, 45, 60].map((m) => <option key={m} value={m}>{m} min</option>)}</select></td>
                <td><button className="icon-btn" aria-label="Remove session" onClick={() => setRows(rows.filter((_, j) => j !== i))}><IconTrash /></button></td>
              </tr>
            ))}
            {!rows.length && <tr><td colSpan={5} className="muted">No sessions. The doctor will not be bookable.</td></tr>}
          </tbody>
        </table>
      </div>
      <div className="row">
        <Button size="sm" onClick={() => setRows([...rows, { weekday: 1, startTime: "17:00", endTime: "20:00", slotMinutes: 15 }])}><IconPlus /> Add session</Button>
        <Button size="sm" variant="ghost" onClick={addWeekdays}>Fill Mon–Sat mornings</Button>
      </div>
    </Modal>
  );
}
