// Shared UI primitives.
import { createContext, useCallback, useContext, useEffect, useId, useRef, useState, type ReactNode } from "react";
import { ApiError } from "../lib/api";
import { humanize } from "../lib/format";
import { IconAlert, IconInfo, IconX } from "./icons";

// ---------- Async data hook ----------
export function useAsync<T>(fn: () => Promise<T>, deps: unknown[]) {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState<ApiError | null>(null);
  const [loading, setLoading] = useState(true);
  const seq = useRef(0);
  const run = useCallback(async () => {
    const my = ++seq.current;
    setLoading(true);
    try {
      const d = await fn();
      if (my === seq.current) {
        setData(d);
        setError(null);
      }
    } catch (e) {
      if (my === seq.current) setError(e instanceof ApiError ? e : new ApiError(0, "UNKNOWN", String(e)));
    } finally {
      if (my === seq.current) setLoading(false);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
  useEffect(() => {
    run();
  }, [run]);
  return { data, error, loading, reload: run, setData };
}

// ---------- Toasts ----------
type Toast = { id: number; text: string; kind: "ok" | "error" };
const ToastCtx = createContext<(text: string, kind?: "ok" | "error") => void>(() => {});
export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<Toast[]>([]);
  const push = useCallback((text: string, kind: "ok" | "error" = "ok") => {
    const id = Date.now() + Math.random();
    setToasts((t) => [...t, { id, text, kind }]);
    setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), 4200);
  }, []);
  return (
    <ToastCtx.Provider value={push}>
      {children}
      <div className="toasts" role="status" aria-live="polite">
        {toasts.map((t) => (
          <div key={t.id} className={`toast ${t.kind === "error" ? "error" : ""}`}>{t.text}</div>
        ))}
      </div>
    </ToastCtx.Provider>
  );
}
export const useToast = () => useContext(ToastCtx);

// ---------- Layout ----------
export function PageHeader({ title, subtitle, actions }: { title: ReactNode; subtitle?: ReactNode; actions?: ReactNode }) {
  return (
    <div className="page-head">
      <div>
        <h1>{title}</h1>
        {subtitle && <p>{subtitle}</p>}
      </div>
      {actions && <div className="page-actions">{actions}</div>}
    </div>
  );
}

export function Loading({ label = "Loading" }: { label?: string }) {
  return (
    <div className="loading-block" role="status">
      <span className="spinner" /> {label}…
    </div>
  );
}

export function Empty({ title, children }: { title: string; children?: ReactNode }) {
  return (
    <div className="empty">
      <strong>{title}</strong>
      {children}
    </div>
  );
}

export function Banner({ kind = "info", children }: { kind?: "info" | "warn" | "error" | "mock"; children: ReactNode }) {
  return (
    <div className={`banner banner-${kind}`} role={kind === "error" ? "alert" : undefined}>
      {kind === "error" || kind === "warn" ? <IconAlert /> : <IconInfo />}
      <div>{children}</div>
    </div>
  );
}

export function ErrorBanner({ error }: { error: ApiError | null | undefined }) {
  if (!error) return null;
  return <Banner kind="error">{error.message}</Banner>;
}

// ---------- Buttons ----------
type BtnProps = React.ButtonHTMLAttributes<HTMLButtonElement> & { variant?: "primary" | "secondary" | "ghost" | "danger"; size?: "sm"; busy?: boolean };
export function Button({ variant = "secondary", size, busy, children, className = "", disabled, ...rest }: BtnProps) {
  return (
    <button className={`btn btn-${variant} ${size === "sm" ? "btn-sm" : ""} ${className}`} disabled={disabled || busy} {...rest}>
      {busy && <span className="spinner" style={{ width: 14, height: 14 }} />}
      {children}
    </button>
  );
}

// ---------- Form fields ----------
interface FieldProps {
  label: string;
  error?: string;
  hint?: ReactNode;
  required?: boolean;
  className?: string;
  children: (id: string, describedBy?: string) => ReactNode;
}
export function Field({ label, error, hint, required, className = "", children }: FieldProps) {
  const id = useId();
  const descId = error || hint ? `${id}-d` : undefined;
  return (
    <div className={`field ${className}`}>
      <label htmlFor={id}>
        {label}
        {required && <span className="req" aria-hidden="true">*</span>}
      </label>
      {children(id, descId)}
      {error ? <span className="err" id={descId}>{error}</span> : hint ? <span className="hint" id={descId}>{hint}</span> : null}
    </div>
  );
}

export function TextField(props: {
  label: string; value: string | number | undefined | null; onChange: (v: string) => void; error?: string; hint?: ReactNode;
  required?: boolean; type?: string; placeholder?: string; className?: string; disabled?: boolean; inputMode?: React.HTMLAttributes<HTMLInputElement>["inputMode"];
  maxLength?: number; autoFocus?: boolean; autoComplete?: string; unit?: string; min?: string; max?: string; step?: string; name?: string;
}) {
  const { label, value, onChange, error, hint, required, className, unit, ...rest } = props;
  return (
    <Field label={label} error={error} hint={hint} required={required} className={className}>
      {(id, d) => {
        const input = (
          <input id={id} className="input" value={value ?? ""} onChange={(e) => onChange(e.target.value)} aria-invalid={!!error}
            aria-describedby={d} aria-required={required} {...rest} />
        );
        return unit ? <div className="unit-input">{input}<span className="unit">{unit}</span></div> : input;
      }}
    </Field>
  );
}

export function SelectField(props: {
  label: string; value: string | undefined | null; onChange: (v: string) => void; options: (string | { value: string; label: string })[];
  error?: string; hint?: ReactNode; required?: boolean; placeholder?: string; className?: string; disabled?: boolean;
}) {
  const { label, value, onChange, options, error, hint, required, placeholder, className, disabled } = props;
  return (
    <Field label={label} error={error} hint={hint} required={required} className={className}>
      {(id, d) => (
        <select id={id} className="select" value={value ?? ""} onChange={(e) => onChange(e.target.value)} aria-invalid={!!error}
          aria-describedby={d} disabled={disabled}>
          {placeholder !== undefined && <option value="">{placeholder}</option>}
          {options.map((o) =>
            typeof o === "string" ? <option key={o} value={o}>{humanize(o)}</option> : <option key={o.value} value={o.value}>{o.label}</option>,
          )}
        </select>
      )}
    </Field>
  );
}

export function TextArea(props: {
  label: string; value: string | undefined | null; onChange: (v: string) => void; error?: string; hint?: ReactNode; rows?: number;
  placeholder?: string; className?: string; disabled?: boolean; required?: boolean;
}) {
  const { label, value, onChange, error, hint, rows = 3, className, required, ...rest } = props;
  return (
    <Field label={label} error={error} hint={hint} className={className} required={required}>
      {(id, d) => (
        <textarea id={id} className="textarea" rows={rows} value={value ?? ""} onChange={(e) => onChange(e.target.value)}
          aria-invalid={!!error} aria-describedby={d} {...rest} />
      )}
    </Field>
  );
}

// ---------- Badges ----------
const STATUS_COLOURS: Record<string, string> = {
  SCHEDULED: "b-blue", CHECKED_IN: "b-amber", IN_CONSULTATION: "b-violet", COMPLETED: "b-green", CANCELLED: "b-grey", NO_SHOW: "b-red",
  WAITING: "b-amber", CALLED: "b-blue", DONE: "b-green", SKIPPED: "b-grey",
  DRAFT: "b-amber", FINALIZED: "b-green",
  PENDING: "b-amber", PARTIALLY_PAID: "b-blue", PAID: "b-green", REFUNDED: "b-grey",
  REQUESTED: "b-amber", GRANTED: "b-green", DENIED: "b-red", EXPIRED: "b-grey", REVOKED: "b-grey", FAILED: "b-red",
  VERIFIED: "b-green", UNLINKED: "b-grey", SUCCESS: "b-green", RETRY_SCHEDULED: "b-amber", INITIATED: "b-blue",
  ACTIVE: "b-red", INACTIVE: "b-grey", ENTERED_IN_ERROR: "b-grey", DENIED_ACCESS: "b-red",
  URGENT: "b-red", ROUTINE: "b-grey", WALK_IN: "b-violet", FOLLOW_UP: "b-blue",
};
export function StatusBadge({ status, label }: { status?: string; label?: string }) {
  if (!status) return null;
  return <span className={`badge ${STATUS_COLOURS[status] ?? ""}`}>{label ?? humanize(status)}</span>;
}

export function MockFlag({ children = "Mock ABDM" }: { children?: ReactNode }) {
  return <span className="mock-flag" title="Simulated by the mock ABDM adapter — not a real ABDM transaction">{children}</span>;
}

// ---------- Modal ----------
export function Modal({ title, onClose, children, footer, wide }: { title: string; onClose: () => void; children: ReactNode; footer?: ReactNode; wide?: boolean }) {
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const prev = document.activeElement as HTMLElement | null;
    const first = ref.current?.querySelector<HTMLElement>("input, select, textarea, button:not(.icon-btn)");
    first?.focus();
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("keydown", onKey);
      prev?.focus();
    };
  }, [onClose]);
  return (
    <div className="modal-back" onMouseDown={(e) => e.target === e.currentTarget && onClose()}>
      <div className={`modal ${wide ? "wide" : ""}`} role="dialog" aria-modal="true" aria-label={title} ref={ref}>
        <div className="modal-head">
          <h2>{title}</h2>
          <button className="icon-btn" onClick={onClose} aria-label="Close"><IconX /></button>
        </div>
        <div className="modal-body">{children}</div>
        {footer && <div className="modal-foot">{footer}</div>}
      </div>
    </div>
  );
}

export function Tabs<T extends string>({ tabs, value, onChange }: { tabs: { id: T; label: ReactNode }[]; value: T; onChange: (t: T) => void }) {
  return (
    <div className="tabs" role="tablist">
      {tabs.map((t) => (
        <button key={t.id} role="tab" className="tab" aria-selected={value === t.id} onClick={() => onChange(t.id)}>{t.label}</button>
      ))}
    </div>
  );
}

export function Segmented<T extends string>({ options, value, onChange, label }: { options: { id: T; label: string }[]; value: T; onChange: (v: T) => void; label: string }) {
  return (
    <div className="segmented" role="group" aria-label={label}>
      {options.map((o) => (
        <button key={o.id} aria-pressed={value === o.id} onClick={() => onChange(o.id)}>{o.label}</button>
      ))}
    </div>
  );
}

/** Generic async search-select (patients, medicines, investigations, diagnoses). */
export function Combo<T>({ label, placeholder, search, render, onPick, hint, error, autoFocus, minChars = 1, className }: {
  label: string; placeholder?: string; search: (q: string) => Promise<T[]>; render: (item: T) => ReactNode; onPick: (item: T) => void;
  hint?: ReactNode; error?: string; autoFocus?: boolean; minChars?: number; className?: string;
}) {
  const [q, setQ] = useState("");
  const [items, setItems] = useState<T[]>([]);
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(0);
  const listId = useId();
  useEffect(() => {
    if (q.trim().length < minChars) {
      setItems([]);
      return;
    }
    const t = setTimeout(() => search(q.trim()).then((r) => { setItems(r); setActive(0); setOpen(true); }).catch(() => setItems([])), 180);
    return () => clearTimeout(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [q]);
  const pick = (it: T) => {
    onPick(it);
    setQ("");
    setItems([]);
    setOpen(false);
  };
  return (
    <Field label={label} hint={hint} error={error} className={className}>
      {(id, d) => (
        <div className="combo">
          <input id={id} className="input" value={q} placeholder={placeholder} autoFocus={autoFocus} autoComplete="off"
            role="combobox" aria-expanded={open && items.length > 0} aria-controls={listId} aria-describedby={d} aria-invalid={!!error}
            onChange={(e) => setQ(e.target.value)} onFocus={() => items.length && setOpen(true)} onBlur={() => setTimeout(() => setOpen(false), 150)}
            onKeyDown={(e) => {
              if (e.key === "ArrowDown") { e.preventDefault(); setActive((a) => Math.min(a + 1, items.length - 1)); }
              else if (e.key === "ArrowUp") { e.preventDefault(); setActive((a) => Math.max(a - 1, 0)); }
              else if (e.key === "Enter" && open && items[active]) { e.preventDefault(); pick(items[active]); }
              else if (e.key === "Escape") setOpen(false);
            }} />
          {open && items.length > 0 && (
            <ul className="combo-list" id={listId} role="listbox">
              {items.map((it, i) => (
                <li key={i} role="option" aria-selected={i === active} onMouseDown={(e) => { e.preventDefault(); pick(it); }}>{render(it)}</li>
              ))}
            </ul>
          )}
        </div>
      )}
    </Field>
  );
}

/** Converts an ApiError's field map into a dotted-key errors object for forms. */
export function fieldErrors(e: unknown): Record<string, string> {
  return e instanceof ApiError ? e.fields : {};
}
