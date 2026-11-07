import { useEffect, useId, useState } from "react";
import { api } from "../lib/api";
import type { Page, Patient } from "../lib/types";
import { ageSex } from "../lib/format";
import { navigate } from "../lib/router";
import { IconSearch } from "../components/icons";

/** Global quick search in the top bar. Enter opens the directory with the query. */
export function PatientSearchBox() {
  const [q, setQ] = useState("");
  const [items, setItems] = useState<Patient[]>([]);
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(0);
  const listId = useId();

  useEffect(() => {
    if (q.trim().length < 2) {
      setItems([]);
      return;
    }
    const t = setTimeout(() => {
      api.get<Page<Patient>>("/patients", { q: q.trim(), size: 6 }).then((r) => { setItems(r.items); setOpen(true); setActive(0); }).catch(() => {});
    }, 220);
    return () => clearTimeout(t);
  }, [q]);

  const go = (p: Patient) => {
    setQ("");
    setOpen(false);
    navigate(`/patients/${p.id}`);
  };

  return (
    <div className="combo" style={{ position: "relative" }}>
      <label htmlFor="global-search" className="sr-only">Find a patient</label>
      <span style={{ position: "absolute", left: 10, top: 9, color: "var(--ink-3)" }}><IconSearch width={16} height={16} /></span>
      <input id="global-search" className="input" style={{ paddingLeft: 32 }} placeholder="Find patient by name, mobile, patient ID or ABHA"
        value={q} autoComplete="off" role="combobox" aria-expanded={open && items.length > 0} aria-controls={listId}
        onChange={(e) => setQ(e.target.value)} onBlur={() => setTimeout(() => setOpen(false), 150)}
        onKeyDown={(e) => {
          if (e.key === "ArrowDown") { e.preventDefault(); setActive((a) => Math.min(a + 1, items.length - 1)); }
          else if (e.key === "ArrowUp") { e.preventDefault(); setActive((a) => Math.max(a - 1, 0)); }
          else if (e.key === "Enter") {
            e.preventDefault();
            if (open && items[active]) go(items[active]);
            else if (q.trim()) { navigate(`/patients?q=${encodeURIComponent(q.trim())}`); setQ(""); }
          } else if (e.key === "Escape") setOpen(false);
        }} />
      {open && items.length > 0 && (
        <ul className="combo-list" id={listId} role="listbox">
          {items.map((p, i) => (
            <li key={p.id} role="option" aria-selected={i === active} onMouseDown={(e) => { e.preventDefault(); go(p); }}>
              <strong>{p.fullName}</strong> <span className="muted">{ageSex(p.ageYears, p.gender)}</span>
              <div className="cell-sub">{p.patientNumber}, {p.mobile}</div>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
