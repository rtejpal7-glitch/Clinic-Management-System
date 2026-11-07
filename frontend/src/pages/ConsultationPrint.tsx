import { api } from "../lib/api";
import type { Allergy, Consultation, Patient, Settings } from "../lib/types";
import { ageSex, dateOnly, dateTime, humanize } from "../lib/format";
import { navigate } from "../lib/router";
import { Banner, Button, ErrorBanner, Loading, PageHeader, useAsync } from "../components/ui";
import { IconPrinter } from "../components/icons";

export function ConsultationPrintPage({ id, kind }: { id: string; kind: "rx" | "summary" }) {
  const data = useAsync(async () => {
    const c = await api.get<Consultation>(`/consultations/${id}`);
    const [p, s, a] = await Promise.all([
      api.get<Patient>(`/patients/${c.patientId}`),
      api.get<Settings>("/settings"),
      api.get<Allergy[]>(`/patients/${c.patientId}/allergies`),
    ]);
    return { c, p, s, a: a.filter((x) => x.status === "ACTIVE") };
  }, [id]);
  if (data.loading) return <Loading />;
  if (data.error) return <ErrorBanner error={data.error} />;
  const { c, p, s, a } = data.data!;
  const v = c.vitals;

  return (
    <>
      <PageHeader title={kind === "rx" ? "Prescription" : "Visit summary"} subtitle={`${c.patientName}, ${dateOnly(c.startedAt)}`}
        actions={<><Button onClick={() => navigate(`/consultations/${id}`)}>Back to consultation</Button><Button variant="primary" onClick={() => window.print()}><IconPrinter /> Print</Button></>} />
      {c.status !== "FINALIZED" && <div className="no-print"><Banner kind="warn">This consultation is still a draft. Finalize it before giving a printed copy to the patient.</Banner></div>}
      <article className="print-doc">
        <header>
          <div>
            <div className="clinic-name">{s.clinicName}</div>
            <div className="small">{[s.address, s.city, s.state, s.pincode].filter(Boolean).join(", ")}</div>
            <div className="small">{s.phone && `Phone ${s.phone}`} {s.registrationNumber && ` Reg. ${s.registrationNumber}`}</div>
          </div>
          <div className="right small">
            <strong>{c.doctorName}</strong>
            <div>{c.doctorQualification}</div>
            {c.doctorRegistration && <div>Reg. no. {c.doctorRegistration}</div>}
          </div>
        </header>
        <table>
          <tbody>
            <tr><td><strong>{p.fullName}</strong>, {ageSex(p.ageYears, p.gender)}</td><td>Patient ID {p.patientNumber}</td><td className="right">{dateTime(c.startedAt)}</td></tr>
            <tr><td colSpan={3}>Allergies: {a.length ? a.map((x) => x.substance).join(", ") : "None recorded"}</td></tr>
            {v && <tr><td colSpan={3} className="tabular">
              {v.bpSystolic ? `BP ${v.bpSystolic}/${v.bpDiastolic} mmHg   ` : ""}{v.pulse ? `Pulse ${v.pulse}/min   ` : ""}{v.temperatureC ? `Temp ${v.temperatureC} °C   ` : ""}
              {v.spo2 ? `SpO₂ ${v.spo2}%   ` : ""}{v.weightKg ? `Weight ${v.weightKg} kg   ` : ""}{v.bmi ? `BMI ${v.bmi}` : ""}
            </td></tr>}
          </tbody>
        </table>
        {kind === "summary" && (
          <>
            {c.chiefComplaint && <p><strong>Chief complaint:</strong> {c.chiefComplaint}</p>}
            {c.historyPresentIllness && <p><strong>History:</strong> {c.historyPresentIllness}</p>}
            {c.examinationNotes && <p><strong>Examination:</strong> {c.examinationNotes}</p>}
          </>
        )}
        <p><strong>Diagnosis:</strong> {c.diagnoses.map((d) => `${d.display}${d.code ? ` (${d.code})` : ""}`).join("; ") || "—"}</p>
        <div className="rx-symbol">℞</div>
        {c.prescription.items.length ? (
          <table>
            <thead><tr><th>#</th><th>Medicine</th><th>Dose</th><th>Frequency</th><th>Duration</th><th>Instructions</th></tr></thead>
            <tbody>
              {c.prescription.items.map((it, i) => (
                <tr key={i}>
                  <td>{i + 1}</td>
                  <td><strong>{it.medicineName}</strong> {it.strength} {it.dosageForm ? humanize(it.dosageForm) : ""}<div className="small">{humanize(it.route)}</div></td>
                  <td>{it.dose}</td><td>{it.frequency}</td><td>{it.durationDays ? `${it.durationDays} days` : "—"}</td><td>{it.instructions ?? ""}</td>
                </tr>
              ))}
            </tbody>
          </table>
        ) : <p className="small">No medicines prescribed.</p>}
        {c.prescription.investigations.length > 0 && <p><strong>Investigations:</strong> {c.prescription.investigations.filter((o) => o.status !== "CANCELLED").map((o) => `${o.name}${o.priority === "URGENT" ? " (urgent)" : ""}`).join(", ")}</p>}
        {c.prescription.advice && <p><strong>Advice:</strong> {c.prescription.advice}</p>}
        {kind === "summary" && c.treatmentPlan && <p><strong>Plan:</strong> {c.treatmentPlan}</p>}
        {(c.followUpInstructions || c.followUpDate) && <p><strong>Follow-up:</strong> {c.followUpInstructions ?? ""} {c.followUpDate ? `Review on ${dateOnly(c.followUpDate)}.` : ""}</p>}
        {kind === "summary" && c.addenda.length > 0 && (
          <div><strong>Addenda</strong>{c.addenda.map((ad) => <p key={ad.id} className="small">{dateTime(ad.createdAt)}, {ad.authorName}: {ad.content}</p>)}</div>
        )}
        <div className="sign">
          <div style={{ height: 36 }} />
          {c.doctorName}<br />{c.status === "FINALIZED" ? `Electronically finalized ${dateTime(c.finalizedAt)}` : "DRAFT — not finalized"}
        </div>
        <footer>Generated by ClinicFlow (proof of concept) on {dateTime(new Date().toISOString())}. Consultation ID {c.id}.</footer>
      </article>
    </>
  );
}
