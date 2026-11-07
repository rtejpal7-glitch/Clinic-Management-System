import { useEffect, useState, type FormEvent } from "react";
import { api, ApiError } from "../lib/api";
import type { Patient } from "../lib/types";
import { validatePatient, dobFromAge } from "../lib/validation";
import { navigate } from "../lib/router";
import { ageSex } from "../lib/format";
import { Banner, Button, ErrorBanner, Loading, Modal, PageHeader, SelectField, TextField, useToast, Segmented } from "../components/ui";

const STATES = ["Andhra Pradesh", "Arunachal Pradesh", "Assam", "Bihar", "Chhattisgarh", "Goa", "Gujarat", "Haryana", "Himachal Pradesh",
  "Jharkhand", "Karnataka", "Kerala", "Madhya Pradesh", "Maharashtra", "Manipur", "Meghalaya", "Mizoram", "Nagaland", "Odisha", "Punjab",
  "Rajasthan", "Sikkim", "Tamil Nadu", "Telangana", "Tripura", "Uttar Pradesh", "Uttarakhand", "West Bengal", "Andaman and Nicobar Islands",
  "Chandigarh", "Dadra and Nagar Haveli and Daman and Diu", "Delhi", "Jammu and Kashmir", "Ladakh", "Lakshadweep", "Puducherry"];

type Form = Partial<Patient> & { fullName: string; dateOfBirth: string; gender: string; mobile: string };
const empty: Form = { fullName: "", dateOfBirth: "", gender: "", mobile: "", state: "Karnataka" };

interface Candidate { id: string; patientNumber: string; fullName: string; dateOfBirth: string; gender: string; mobile: string; score: number; matchedOn: string[] }

export function PatientFormPage({ patientId }: { patientId?: string }) {
  const toast = useToast();
  const editing = !!patientId;
  const [form, setForm] = useState<Form>(empty);
  const [loaded, setLoaded] = useState(!editing);
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);
  const [apiErr, setApiErr] = useState<ApiError | null>(null);
  const [dupes, setDupes] = useState<Candidate[] | null>(null);
  const [overrideReason, setOverrideReason] = useState("");
  const [changeReason, setChangeReason] = useState("");
  const [dobMode, setDobMode] = useState<"dob" | "age">("dob");
  const [age, setAge] = useState("");

  useEffect(() => {
    if (!patientId) return;
    api.get<Patient>(`/patients/${patientId}`).then((p) => { setForm(p); setLoaded(true); }).catch((e) => setApiErr(e));
  }, [patientId]);

  const set = (k: keyof Form) => (v: string) => setForm((f) => ({ ...f, [k]: v }));

  const payload = (extra: Record<string, unknown> = {}) => {
    const dob = dobMode === "age" && age ? dobFromAge(Number(age)) : form.dateOfBirth;
    return { ...form, dateOfBirth: dob, dobIsEstimated: dobMode === "age" ? true : form.dobIsEstimated ?? false, ...extra };
  };

  const submit = async (e?: FormEvent, extra: Record<string, unknown> = {}) => {
    e?.preventDefault();
    setApiErr(null);
    const body = payload(extra);
    const errs = validatePatient({ ...body, email: form.email, pincode: form.pincode, emergencyContactPhone: form.emergencyContactPhone } as any);
    if (dobMode === "age" && (!age || Number(age) < 0 || Number(age) > 125)) errs.dateOfBirth = "Enter age in years";
    setErrors(errs);
    if (Object.keys(errs).length) return;
    setBusy(true);
    try {
      if (editing) {
        const p = await api.put<Patient>(`/patients/${patientId}`, { ...body, changeReason: changeReason || undefined });
        toast("Patient details updated");
        navigate(`/patients/${p.id}`);
      } else {
        const p = await api.post<Patient>("/patients", body);
        toast(`Registered ${p.fullName} as ${p.patientNumber}`);
        navigate(`/patients/${p.id}`);
      }
    } catch (err) {
      if (err instanceof ApiError && err.code === "POSSIBLE_DUPLICATE") setDupes(err.data?.candidates ?? []);
      else if (err instanceof ApiError) {
        setErrors(err.fields);
        setApiErr(err);
      }
    } finally {
      setBusy(false);
    }
  };

  if (!loaded) return apiErr ? <ErrorBanner error={apiErr} /> : <Loading />;
  return (
    <>
      <PageHeader title={editing ? `Edit ${form.fullName}` : "Register patient"}
        subtitle={editing ? `${form.patientNumber}. The patient ID never changes.` : "A patient ID is generated automatically. Required fields are marked."} />
      {apiErr && !Object.keys(apiErr.fields).length && <ErrorBanner error={apiErr} />}
      {apiErr?.code === "STALE_VERSION" && <Banner kind="warn">Someone else updated this record. <a href={`#/patients/${patientId}`}>Reload the patient</a> and make your change again.</Banner>}
      <form onSubmit={submit} noValidate className="panel">
        <div className="panel-body">
          <fieldset className="fieldset">
            <legend>Identity</legend>
            <div className="grid g3">
              <TextField label="Full name" required value={form.fullName} onChange={set("fullName")} error={errors.fullName} className="span2" autoFocus={!editing}
                hint="As on an ID document" autoComplete="off" />
              <SelectField label="Gender" required value={form.gender} onChange={set("gender")} error={errors.gender} placeholder="Select"
                options={[{ value: "FEMALE", label: "Female" }, { value: "MALE", label: "Male" }, { value: "OTHER", label: "Other" }, { value: "UNDISCLOSED", label: "Prefer not to say" }]} />
              <div className="field">
                <span className="row" style={{ justifyContent: "space-between" }}>
                  <label style={{ fontSize: "var(--fs-sm)", fontWeight: 600, color: "var(--ink-2)" }}>Date of birth<span className="req">*</span></label>
                  <Segmented label="Date of birth entry" value={dobMode} onChange={setDobMode} options={[{ id: "dob", label: "Date" }, { id: "age", label: "Age only" }]} />
                </span>
                {dobMode === "dob" ? (
                  <input className="input" type="date" aria-label="Date of birth" value={form.dateOfBirth} max={new Date().toISOString().slice(0, 10)}
                    onChange={(e) => setForm((f) => ({ ...f, dateOfBirth: e.target.value, dobIsEstimated: false }))} aria-invalid={!!errors.dateOfBirth} />
                ) : (
                  <div className="unit-input"><input className="input" inputMode="numeric" aria-label="Age in years" value={age} onChange={(e) => setAge(e.target.value.replace(/\D/g, ""))} /><span className="unit">years</span></div>
                )}
                {errors.dateOfBirth ? <span className="err">{errors.dateOfBirth}</span> : <span className="hint">{dobMode === "age" ? "Recorded as an estimated date of birth" : form.dobIsEstimated ? "Estimated date of birth" : " "}</span>}
              </div>
              <SelectField label="Blood group" value={form.bloodGroup} onChange={set("bloodGroup")} placeholder="Unknown" options={["A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-"].map((b) => ({ value: b, label: b }))} />
              <SelectField label="Marital status" value={form.maritalStatus} onChange={set("maritalStatus")} placeholder="Not recorded" options={["SINGLE", "MARRIED", "DIVORCED", "WIDOWED", "OTHER"]} />
            </div>
          </fieldset>
          <fieldset className="fieldset">
            <legend>Contact</legend>
            <div className="grid g3">
              <TextField label="Mobile number" required value={form.mobile} onChange={(v) => set("mobile")(v.replace(/\D/g, "").slice(0, 10))} error={errors.mobile}
                inputMode="numeric" hint="10 digits. Family members may share a number." />
              <TextField label="Email" type="email" value={form.email} onChange={set("email")} error={errors.email} />
              <TextField label="Preferred language" value={form.preferredLanguage} onChange={set("preferredLanguage")} placeholder="e.g. Kannada" />
              <TextField label="Address" value={form.addressLine} onChange={set("addressLine")} className="span-all" placeholder="House, street, area" />
              <TextField label="City / town" value={form.city} onChange={set("city")} />
              <TextField label="District" value={form.district} onChange={set("district")} />
              <SelectField label="State / UT" value={form.state} onChange={set("state")} placeholder="Select" options={STATES.map((s) => ({ value: s, label: s }))} />
              <TextField label="PIN code" value={form.pincode} onChange={(v) => set("pincode")(v.replace(/\D/g, "").slice(0, 6))} error={errors.pincode} inputMode="numeric" />
              <TextField label="Occupation" value={form.occupation} onChange={set("occupation")} />
            </div>
          </fieldset>
          <fieldset className="fieldset">
            <legend>Emergency contact</legend>
            <div className="grid g3">
              <TextField label="Name" value={form.emergencyContactName} onChange={set("emergencyContactName")} />
              <TextField label="Relationship" value={form.emergencyContactRelation} onChange={set("emergencyContactRelation")} placeholder="e.g. Spouse" />
              <TextField label="Mobile" value={form.emergencyContactPhone} onChange={(v) => set("emergencyContactPhone")(v.replace(/\D/g, "").slice(0, 10))} error={errors.emergencyContactPhone} inputMode="numeric" />
            </div>
          </fieldset>
          {editing && (
            <fieldset className="fieldset" style={{ marginBottom: 0 }}>
              <legend>Reason for change</legend>
              <TextField label="Why are these details changing?" value={changeReason} onChange={setChangeReason} hint="Stored in the patient's change history" />
            </fieldset>
          )}
        </div>
        <div className="modal-foot" style={{ justifyContent: "flex-start" }}>
          <Button variant="primary" type="submit" busy={busy}>{editing ? "Save changes" : "Register patient"}</Button>
          <Button type="button" variant="ghost" onClick={() => navigate(editing ? `/patients/${patientId}` : "/patients")}>Cancel</Button>
        </div>
      </form>

      {dupes && (
        <Modal title="This patient may already be registered" wide onClose={() => setDupes(null)}
          footer={
            <>
              <Button onClick={() => setDupes(null)}>Back to form</Button>
              <Button variant="danger" disabled={overrideReason.trim().length < 5} busy={busy}
                onClick={() => submit(undefined, { confirmNotDuplicate: true, duplicateOverrideReason: overrideReason })}>
                Register as a new patient
              </Button>
            </>
          }>
          <p>These existing records match on the details shown. Open the matching record instead of creating a duplicate.</p>
          <div className="table-wrap" style={{ marginBottom: 16 }}>
            <table className="data">
              <thead><tr><th>Patient</th><th>Age / sex</th><th>Mobile</th><th>Matched on</th><th>Score</th><th /></tr></thead>
              <tbody>
                {dupes.map((c) => (
                  <tr key={c.id}>
                    <td><div className="cell-main">{c.fullName}</div><div className="cell-sub">{c.patientNumber}</div></td>
                    <td>{ageSex(new Date().getFullYear() - Number(c.dateOfBirth.slice(0, 4)), c.gender)}</td>
                    <td className="num">{c.mobile}</td>
                    <td>{c.matchedOn.map((m) => ({ mobile: "Mobile", dateOfBirth: "Date of birth", name: "Name", gender: "Gender" } as Record<string, string>)[m] ?? m).join(", ")}</td>
                    <td className="num">{c.score}</td>
                    <td><Button size="sm" variant="primary" onClick={() => navigate(`/patients/${c.id}`)}>Open record</Button></td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <TextField label="If this is a different person, record why" value={overrideReason} onChange={setOverrideReason}
            hint="For example: twin sibling, verified with Aadhaar card. Recorded in the audit log." />
        </Modal>
      )}
    </>
  );
}
