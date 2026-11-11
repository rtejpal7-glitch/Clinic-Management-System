// This clinic's participation in the ClinicFlow network, and (for platform
// administrators) the clinics hosted on this installation.
import { useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import { dateOnly, dateTime, humanize } from "../lib/format";
import { Banner, Button, ErrorBanner, Loading, Modal, PageHeader, SelectField, TextField, useAsync, useToast } from "../components/ui";

interface Facility {
  id: string; code: string; name: string; specialty: string; city?: string; state?: string; hfrId?: string; isActive: boolean;
  networkSharingEnabled: boolean; networkAgreementRef?: string; networkEnabledAt?: string; userCount: number; createdAt: string;
}

export function ClinicsPage() {
  const { can, user } = useAuth();
  const toast = useToast();
  const current = useAsync(() => api.get<Facility>("/facilities/current"), []);
  const all = useAsync(() => (can("platform.manage") ? api.get<Facility[]>("/facilities") : Promise.resolve([] as Facility[])), []);
  const [joining, setJoining] = useState(false);
  const [creating, setCreating] = useState(false);
  const [ref, setRef] = useState("");
  const [ack, setAck] = useState(false);
  const [err, setErr] = useState<ApiError | null>(null);

  const setSharing = async (enabled: boolean) => {
    setErr(null);
    try {
      await api.put("/facilities/current/network", enabled ? { enabled, agreementRef: ref } : { enabled });
      toast(enabled ? "Clinic joined the ClinicFlow network" : "Clinic left the network; its de-identified records were withdrawn");
      setJoining(false); setRef(""); setAck(false); current.reload(); all.reload();
    } catch (e) { setErr(e as ApiError); }
  };

  if (current.loading && !current.data) return <Loading />;
  if (current.error) return <ErrorBanner error={current.error} />;
  const f = current.data!;
  return (
    <>
      <PageHeader title="Clinic & network" subtitle={`${f.name} (${f.code}) · ${humanize(f.specialty)}`} />
      <div className="split">
        <section className="panel">
          <div className="panel-head"><h3>ClinicFlow network participation</h3>
            {f.networkSharingEnabled ? <span className="badge b-green">Participating</span> : <span className="badge b-grey">Not participating</span>}</div>
          <div className="panel-body stack">
            <p className="small" style={{ margin: 0 }}>
              Participating clinics contribute <strong>de-identified, coded</strong> prescribing records (diagnosis code, severity, age band, medicines and
              regimen, follow-up outcome, month) and in return their doctors can see aggregate patterns from other clinics. No names, patient numbers,
              dates of birth, free text, doctor or clinic identities are shared; clinic and doctor are replaced by keyed pseudonyms. Statistics are shown
              only above minimum sample sizes. Patients can opt out from their record.
            </p>
            {f.networkSharingEnabled ? (
              <>
                <div className="small">Agreement reference <strong>{f.networkAgreementRef}</strong>, joined {dateTime(f.networkEnabledAt)}.</div>
                {can("settings.manage") && <div><Button variant="danger" onClick={() => setSharing(false)}>Leave the network</Button></div>}
              </>
            ) : can("settings.manage") ? (
              <div><Button variant="primary" onClick={() => setJoining(true)}>Join the network…</Button></div>
            ) : null}
            <Banner kind="info">Before joining, the clinic needs a lawful basis and governance for this secondary use (data-sharing agreement, patient notice and opt-out under the Digital Personal Data Protection Act, 2023 and the rules made under it). ClinicFlow records the agreement reference; it does not provide the legal agreement itself.</Banner>
          </div>
        </section>
        <section className="panel">
          <div className="panel-head"><h3>This clinic</h3></div>
          <div className="panel-body">
            <dl className="kv">
              <dt>Code</dt><dd>{f.code}</dd><dt>Specialty</dt><dd>{humanize(f.specialty)}</dd><dt>City</dt><dd>{[f.city, f.state].filter(Boolean).join(", ") || "—"}</dd>
              <dt>HFR ID</dt><dd>{f.hfrId ?? "—"}</dd><dt>Active users</dt><dd>{f.userCount}</dd><dt>Signed in as</dt><dd>{user?.fullName}</dd>
            </dl>
            <p className="small muted" style={{ marginTop: 10 }}>Edit the clinic's name and address in Clinic settings.</p>
          </div>
        </section>
      </div>

      {can("platform.manage") && (
        <section style={{ marginTop: 20 }}>
          <div className="section-title row" style={{ justifyContent: "space-between" }}><h2>Clinics on this installation</h2><Button onClick={() => setCreating(true)}>Add clinic</Button></div>
          <Banner kind="info">Each clinic's patients, appointments, clinical and billing records are isolated by row-level security in the database. Platform administrators manage clinics but see clinical data only for their own clinic.</Banner>
          <div className="table-wrap"><table className="data">
            <thead><tr><th>Clinic</th><th>Code</th><th>Specialty</th><th>Location</th><th className="num">Users</th><th>Network</th><th>Created</th></tr></thead>
            <tbody>{(all.data ?? []).map((x) => (
              <tr key={x.id}><td className="cell-main">{x.name}</td><td className="tabular">{x.code}</td><td>{humanize(x.specialty)}</td>
                <td>{[x.city, x.state].filter(Boolean).join(", ")}</td><td className="num">{x.userCount}</td>
                <td>{x.networkSharingEnabled ? <span className="badge b-green">Yes</span> : <span className="badge b-grey">No</span>}</td><td>{dateOnly(x.createdAt)}</td></tr>
            ))}</tbody>
          </table></div>
        </section>
      )}

      {joining && (
        <Modal title="Join the ClinicFlow network" onClose={() => setJoining(false)}
          footer={<><Button onClick={() => setJoining(false)}>Cancel</Button><Button variant="primary" disabled={!ack || ref.trim().length < 3} onClick={() => setSharing(true)}>Join</Button></>}>
          {err && <Banner kind="error">{err.message}</Banner>}
          <TextField label="Data-sharing agreement / governance approval reference" required value={ref} onChange={setRef} error={err?.fields.agreementRef} placeholder="e.g. DSA-2026-014" />
          <label className="consent-box" style={{ marginTop: 12 }}>
            <input type="checkbox" checked={ack} onChange={(e) => setAck(e.target.checked)} />
            <span>The clinic has an approved agreement for this secondary use, informs patients, and honours opt-outs recorded in ClinicFlow.</span>
          </label>
        </Modal>
      )}
      {creating && <CreateClinicModal onClose={() => setCreating(false)} onSaved={() => { setCreating(false); all.reload(); toast("Clinic created. Its administrator must change the temporary password at first sign-in."); }} />}
    </>
  );
}

function CreateClinicModal({ onClose, onSaved }: { onClose: () => void; onSaved: () => void }) {
  const [v, setV] = useState({ code: "", name: "", specialty: "DERMATOLOGY", city: "", state: "", adminUsername: "", adminFullName: "", adminPassword: "" });
  const [err, setErr] = useState<ApiError | null>(null);
  const [busy, setBusy] = useState(false);
  const set = (p: Partial<typeof v>) => setV({ ...v, ...p });
  const save = async () => {
    setBusy(true); setErr(null);
    try { await api.post("/facilities", { ...v, code: v.code.toUpperCase() }); onSaved(); }
    catch (e) { setErr(e as ApiError); } finally { setBusy(false); }
  };
  const fe = err?.fields ?? {};
  return (
    <Modal title="Add clinic" onClose={onClose} footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" busy={busy} onClick={save}>Create clinic</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        <TextField label="Clinic name" required value={v.name} onChange={(x) => set({ name: x })} error={fe.name} />
        <TextField label="Code" required value={v.code} onChange={(x) => set({ code: x.toUpperCase() })} error={fe.code} hint="2-12 letters/digits" maxLength={12} />
        <SelectField label="Specialty" value={v.specialty} onChange={(x) => set({ specialty: x })} options={["DERMATOLOGY", "GENERAL"]} />
        <TextField label="City" value={v.city} onChange={(x) => set({ city: x })} />
        <TextField label="First administrator: username" required value={v.adminUsername} onChange={(x) => set({ adminUsername: x })} error={fe.adminUsername} />
        <TextField label="Administrator full name" required value={v.adminFullName} onChange={(x) => set({ adminFullName: x })} error={fe.adminFullName} />
        <TextField label="Temporary password" required type="password" value={v.adminPassword} onChange={(x) => set({ adminPassword: x })} error={fe.password} hint="Must be changed at first sign-in" />
      </div>
    </Modal>
  );
}
