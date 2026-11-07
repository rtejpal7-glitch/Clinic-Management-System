import { useEffect, useState } from "react";
import { api } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Page, Patient } from "../lib/types";
import { ageSex, dateOnly } from "../lib/format";
import { navigate } from "../lib/router";
import { Button, Empty, ErrorBanner, Loading, PageHeader, useAsync } from "../components/ui";
import { IconSearch, IconUserPlus } from "../components/icons";

export function PatientDirectoryPage({ initialQuery }: { initialQuery: string }) {
  const { can } = useAuth();
  const [q, setQ] = useState(initialQuery);
  const [debounced, setDebounced] = useState(initialQuery);
  const [page, setPage] = useState(1);
  useEffect(() => {
    const t = setTimeout(() => { setDebounced(q); setPage(1); }, 300);
    return () => clearTimeout(t);
  }, [q]);
  const res = useAsync(() => api.get<Page<Patient>>("/patients", { q: debounced, page, size: 25 }), [debounced, page]);
  const pages = res.data ? Math.max(1, Math.ceil(res.data.total / res.data.size)) : 1;

  return (
    <>
      <PageHeader title="Patient directory" subtitle="Search by name, 10-digit mobile, patient ID (e.g. CF-2026-000123) or a linked ABHA number/address."
        actions={can("patient.create") && <Button variant="primary" onClick={() => navigate("/patients/new")}><IconUserPlus /> Register patient</Button>} />
      <div className="row" style={{ marginBottom: 14 }}>
        <div style={{ position: "relative", flex: 1, maxWidth: 520 }}>
          <span style={{ position: "absolute", left: 10, top: 9, color: "var(--ink-3)" }}><IconSearch width={16} height={16} /></span>
          <label htmlFor="dir-search" className="sr-only">Search patients</label>
          <input id="dir-search" className="input" style={{ paddingLeft: 32 }} value={q} onChange={(e) => setQ(e.target.value)} placeholder="Search patients" autoFocus />
        </div>
        {res.data && <span className="muted small">{res.data.total.toLocaleString("en-IN")} {res.data.total === 1 ? "patient" : "patients"}</span>}
      </div>
      <ErrorBanner error={res.error} />
      <div className="table-wrap">
        {res.loading && !res.data ? <Loading /> : res.data && res.data.items.length === 0 ? (
          <Empty title={debounced ? "No patients match this search" : "No patients registered yet"}>
            {can("patient.create") && <p style={{ marginTop: 8 }}><a href="#/patients/new">Register a new patient</a></p>}
          </Empty>
        ) : (
          <table className="data">
            <thead><tr><th>Patient</th><th>Patient ID</th><th>Age / sex</th><th>Mobile</th><th>Location</th><th>ABHA</th><th>Registered</th></tr></thead>
            <tbody>
              {res.data?.items.map((p) => (
                <tr key={p.id} className="clickable" onClick={() => navigate(`/patients/${p.id}`)}>
                  <td><a href={`#/patients/${p.id}`} className="cell-main" onClick={(e) => e.stopPropagation()}>{p.fullName}</a></td>
                  <td className="num nowrap">{p.patientNumber}</td>
                  <td className="nowrap">{ageSex(p.ageYears, p.gender)}</td>
                  <td className="num">{p.mobile}</td>
                  <td>{[p.city, p.state].filter(Boolean).join(", ") || <span className="muted">—</span>}</td>
                  <td>{p.abhaLinked ? <span className="badge b-green">Linked</span> : <span className="muted small">Not linked</span>}</td>
                  <td className="nowrap muted">{dateOnly(p.createdAt)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      {pages > 1 && (
        <div className="row" style={{ marginTop: 12, justifyContent: "flex-end" }}>
          <Button size="sm" disabled={page <= 1} onClick={() => setPage(page - 1)}>Previous</Button>
          <span className="small muted">Page {page} of {pages}</span>
          <Button size="sm" disabled={page >= pages} onClick={() => setPage(page + 1)}>Next</Button>
        </div>
      )}
    </>
  );
}
