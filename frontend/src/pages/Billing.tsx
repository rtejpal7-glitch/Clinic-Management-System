import { useEffect, useMemo, useState } from "react";
import { api, ApiError } from "../lib/api";
import { useAuth } from "../lib/auth";
import type { Appointment, Bill, Page, Patient, Service, Settings } from "../lib/types";
import { addDays, ageSex, dateTime, humanize, rupees, todayIST, toPaise } from "../lib/format";
import { previewBill } from "../lib/validation";
import { navigate } from "../lib/router";
import {
  Banner, Button, Combo, Empty, ErrorBanner, Loading, Modal, PageHeader, SelectField, StatusBadge, TextField, useAsync, useToast,
} from "../components/ui";
import { IconPlus, IconPrinter, IconTrash } from "../components/icons";

interface DailySummary {
  date: string; billsIssued: number; billedPaise: number; discountPaise: number; taxPaise: number; collectedPaise: number; refundedPaise: number;
  netCollectedPaise: number; outstandingPaise: number; collectedByMethod: Record<string, number>;
}

export function BillingPage() {
  const { can } = useAuth();
  const [date, setDate] = useState(todayIST());
  const [status, setStatus] = useState("");
  const [q, setQ] = useState("");
  const [dq, setDq] = useState("");
  useEffect(() => { const t = setTimeout(() => setDq(q), 300); return () => clearTimeout(t); }, [q]);
  const list = useAsync(() => api.get<Page<Bill>>("/bills", { date: dq ? undefined : date, status, q: dq, size: 100 }), [date, status, dq]);
  const summary = useAsync(() => (can("report.billing") ? api.get<DailySummary>("/reports/billing/daily", { date }) : Promise.resolve(null)), [date]);

  return (
    <>
      <PageHeader title="Billing" subtitle="Outpatient bills and payments. Amounts in Indian rupees."
        actions={can("billing.manage") && <Button variant="primary" onClick={() => navigate("/bills/new")}><IconPlus /> New bill</Button>} />
      <div className="row" style={{ marginBottom: 14 }}>
        <Button size="sm" onClick={() => setDate(addDays(date, -1))}>Previous</Button>
        <input type="date" className="input" style={{ width: 160 }} value={date} onChange={(e) => e.target.value && setDate(e.target.value)} aria-label="Date" />
        <Button size="sm" onClick={() => setDate(addDays(date, 1))}>Next</Button>
        <select className="select" style={{ width: 170 }} value={status} onChange={(e) => setStatus(e.target.value)} aria-label="Status">
          <option value="">Any status</option>
          {["PENDING", "PARTIALLY_PAID", "PAID", "REFUNDED", "CANCELLED"].map((s) => <option key={s} value={s}>{humanize(s)}</option>)}
        </select>
        <input className="input" style={{ width: 260 }} value={q} onChange={(e) => setQ(e.target.value)} placeholder="Invoice number or patient name (all dates)" aria-label="Search bills" />
      </div>
      {summary.data && (
        <div className="metrics" aria-label="Daily summary">
          <div className="metric"><div className="metric-label">Bills issued</div><div className="metric-value">{summary.data.billsIssued}</div></div>
          <div className="metric"><div className="metric-label">Billed</div><div className="metric-value">{rupees(summary.data.billedPaise, { decimals: false })}</div><div className="metric-sub">after {rupees(summary.data.discountPaise, { decimals: false })} discounts</div></div>
          <div className="metric"><div className="metric-label">Collected (net)</div><div className="metric-value">{rupees(summary.data.netCollectedPaise, { decimals: false })}</div>
            <div className="metric-sub">{Object.entries(summary.data.collectedByMethod).map(([m, v]) => `${humanize(m)} ${rupees(v, { decimals: false })}`).join(", ") || "No payments"}</div></div>
          <div className="metric"><div className="metric-label">Refunded</div><div className="metric-value">{rupees(summary.data.refundedPaise, { decimals: false })}</div></div>
          <div className="metric"><div className="metric-label">Outstanding from this day</div><div className="metric-value">{rupees(summary.data.outstandingPaise, { decimals: false })}</div></div>
        </div>
      )}
      <ErrorBanner error={list.error} />
      <div className="table-wrap">
        {list.loading && !list.data ? <Loading /> : !list.data?.items.length ? <Empty title="No bills found" /> : (
          <table className="data">
            <thead><tr><th>Invoice</th><th>Patient</th><th>Created</th><th className="right">Total</th><th className="right">Paid</th><th className="right">Balance</th><th>Status</th></tr></thead>
            <tbody>
              {list.data.items.map((b) => (
                <tr key={b.id} className="clickable" onClick={() => navigate(`/bills/${b.id}`)}>
                  <td className="num"><a className="cell-main" href={`#/bills/${b.id}`} onClick={(e) => e.stopPropagation()}>{b.invoiceNumber}</a></td>
                  <td><div className="cell-main">{b.patientName}</div><div className="cell-sub">{b.patientNumber}</div></td>
                  <td className="nowrap small">{dateTime(b.createdAt)}<div className="cell-sub">{b.createdBy}</div></td>
                  <td className="num right">{rupees(b.totalPaise)}</td>
                  <td className="num right">{rupees(b.paidPaise - b.refundedPaise)}</td>
                  <td className="num right">{b.balancePaise > 0 ? <strong>{rupees(b.balancePaise)}</strong> : rupees(0)}</td>
                  <td><StatusBadge status={b.status} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </>
  );
}

interface Line { serviceId?: string; description: string; quantity: number; price: string; taxRateBp: number }

export function NewBillPage({ patientId, appointmentId }: { patientId: string | null; appointmentId: string | null }) {
  const toast = useToast();
  const services = useAsync(() => api.get<Service[]>("/billing/services"), []);
  const [pt, setPt] = useState<Patient | null>(null);
  const [appt, setAppt] = useState<Appointment | null>(null);
  const [lines, setLines] = useState<Line[]>([]);
  const [discount, setDiscount] = useState("");
  const [discountReason, setDiscountReason] = useState("");
  const [notes, setNotes] = useState("");
  const [err, setErr] = useState<ApiError | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => { if (patientId) api.get<Patient>(`/patients/${patientId}`).then(setPt).catch(() => {}); }, [patientId]);
  useEffect(() => {
    if (!appointmentId || !services.data) return;
    api.get<Appointment>(`/appointments/${appointmentId}`).then(async (a) => {
      setAppt(a);
      // Pre-fill the consultation fee: follow-up rate for follow-ups, doctor's own fee otherwise.
      const docs = await api.get<{ id: string; consultationFeePaise: number; displayName: string }[]>("/doctors");
      const doc = docs.find((d) => d.id === a.doctorId);
      const svc = services.data!.find((s) => s.code === (a.appointmentType === "FOLLOW_UP" ? "CONSULT-FU" : "CONSULT-NEW"));
      setLines([{ serviceId: svc?.id, description: `${svc?.name ?? "Consultation"}${doc ? ` (${doc.displayName})` : ""}`, quantity: 1,
        price: (a.appointmentType !== "FOLLOW_UP" && doc ? doc.consultationFeePaise / 100 : (svc?.pricePaise ?? 0) / 100).toString(), taxRateBp: svc?.taxRateBp ?? 0 }]);
    }).catch(() => {});
  }, [appointmentId, services.data]);

  const parsed = lines.map((l) => ({ quantity: l.quantity, unitPricePaise: toPaise(l.price) ?? 0, taxRateBp: l.taxRateBp }));
  const discountPaise = discount ? toPaise(discount) ?? -1 : 0;
  const totals = useMemo(() => (parsed.length ? previewBill(parsed, Math.max(0, discountPaise)) : null), [JSON.stringify(parsed), discountPaise]); // eslint-disable-line
  const invalidLines = lines.some((l) => toPaise(l.price) === null || l.quantity < 1 || !l.description.trim());
  const discountError = discountPaise < 0 ? "Enter an amount like 50 or 50.00" : totals && discountPaise > totals.subtotal ? "Discount cannot exceed the subtotal" : undefined;

  const addService = (id: string) => {
    const s = services.data?.find((x) => x.id === id);
    if (s) setLines([...lines, { serviceId: s.id, description: s.name, quantity: 1, price: (s.pricePaise / 100).toString(), taxRateBp: s.taxRateBp }]);
  };
  const save = async () => {
    setBusy(true); setErr(null);
    try {
      const b = await api.post<Bill>("/bills", {
        patientId: pt!.id, appointmentId: appt?.id, consultationId: appt?.consultationId,
        items: lines.map((l, i) => ({ serviceId: l.serviceId, description: l.description, quantity: l.quantity, unitPricePaise: parsed[i].unitPricePaise, taxRateBp: l.taxRateBp })),
        discountPaise: discountPaise > 0 ? discountPaise : 0, discountReason: discountReason || undefined, notes: notes || undefined,
      });
      toast(`Bill ${b.invoiceNumber} created`);
      navigate(`/bills/${b.id}`);
    } catch (e) { setErr(e as ApiError); } finally { setBusy(false); }
  };

  return (
    <>
      <PageHeader title="New bill" subtitle={appt ? `For the ${humanize(appt.appointmentType).toLowerCase()} visit with ${appt.doctorName}` : "Add services, then record payment on the next screen."} />
      {err && <Banner kind="error">{err.message}{Object.values(err.fields)[0] ? ` — ${Object.values(err.fields)[0]}` : ""}</Banner>}
      <div className="split">
        <div className="panel"><div className="panel-body stack">
          {pt ? (
            <div className="row" style={{ justifyContent: "space-between" }}>
              <div><strong>{pt.fullName}</strong> <span className="muted small">{pt.patientNumber}, {ageSex(pt.ageYears, pt.gender)}, {pt.mobile}</span></div>
              {!patientId && <Button size="sm" variant="ghost" onClick={() => setPt(null)}>Change patient</Button>}
            </div>
          ) : (
            <Combo<Patient> label="Patient" placeholder="Search by name, mobile or patient ID" autoFocus minChars={2}
              search={(q) => api.get<Page<Patient>>("/patients", { q, size: 8 }).then((r) => r.items)}
              render={(p) => <><strong>{p.fullName}</strong> <span className="muted">{p.patientNumber}, {p.mobile}</span></>} onPick={setPt} />
          )}
          <div>
            <div className="table-wrap" style={{ border: 0 }}>
              <table className="data">
                <thead><tr><th>Item</th><th style={{ width: 80 }}>Qty</th><th style={{ width: 130 }}>Price (₹)</th><th style={{ width: 80 }}>GST</th><th className="right" style={{ width: 110 }}>Amount</th><th style={{ width: 36 }} /></tr></thead>
                <tbody>
                  {lines.map((l, i) => (
                    <tr key={i}>
                      <td><input className="input" aria-label="Description" value={l.description} onChange={(e) => setLines(lines.map((x, j) => (j === i ? { ...x, description: e.target.value } : x)))} /></td>
                      <td><input className="input" aria-label="Quantity" inputMode="numeric" value={l.quantity} onChange={(e) => setLines(lines.map((x, j) => (j === i ? { ...x, quantity: Number(e.target.value.replace(/\D/g, "")) || 0 } : x)))} /></td>
                      <td><input className="input" aria-label="Price" inputMode="decimal" value={l.price} aria-invalid={toPaise(l.price) === null} onChange={(e) => setLines(lines.map((x, j) => (j === i ? { ...x, price: e.target.value } : x)))} /></td>
                      <td className="num">{l.taxRateBp / 100}%</td>
                      <td className="num right">{rupees((toPaise(l.price) ?? 0) * l.quantity)}</td>
                      <td><button className="icon-btn" aria-label="Remove item" onClick={() => setLines(lines.filter((_, j) => j !== i))}><IconTrash /></button></td>
                    </tr>
                  ))}
                  {!lines.length && <tr><td colSpan={6} className="muted">Add a service below.</td></tr>}
                </tbody>
              </table>
            </div>
            <div className="row" style={{ marginTop: 10 }}>
              <select className="select" style={{ width: 300 }} value="" onChange={(e) => e.target.value && addService(e.target.value)} aria-label="Add service">
                <option value="">Add service from catalogue</option>
                {services.data?.map((s) => <option key={s.id} value={s.id}>{s.name} ({rupees(s.pricePaise, { decimals: false })})</option>)}
              </select>
              <Button size="sm" onClick={() => setLines([...lines, { description: "", quantity: 1, price: "", taxRateBp: 0 }])}>Add custom item</Button>
            </div>
          </div>
          <div className="grid g2">
            <TextField label="Discount (₹)" value={discount} onChange={setDiscount} inputMode="decimal" error={discountError} />
            <TextField label="Discount reason" value={discountReason} onChange={setDiscountReason} required={discountPaise > 0} placeholder="e.g. Senior citizen"
              error={discountPaise > 0 && discountReason.trim().length < 3 ? "Required when giving a discount" : undefined} />
            <TextField label="Notes on invoice" value={notes} onChange={setNotes} className="span-all" />
          </div>
        </div></div>
        <div className="panel">
          <div className="panel-head"><h3>Total</h3></div>
          <div className="panel-body">
            <dl className="kv" style={{ gridTemplateColumns: "1fr auto" }}>
              <dt>Subtotal</dt><dd className="num right">{rupees(totals?.subtotal ?? 0)}</dd>
              <dt>Discount</dt><dd className="num right">− {rupees(totals?.discount ?? 0)}</dd>
              <dt>GST</dt><dd className="num right">{rupees(totals?.tax ?? 0)}</dd>
              <dt style={{ color: "var(--ink)", fontWeight: 700 }}>Amount due</dt><dd className="num right" style={{ fontWeight: 700, fontSize: "var(--fs-lg)" }}>{rupees(totals?.total ?? 0)}</dd>
            </dl>
            <p className="small muted" style={{ marginTop: 12 }}>Healthcare services are generally GST-exempt; tax applies only to items configured with a rate. The server recalculates all amounts.</p>
            <Button variant="primary" style={{ width: "100%", marginTop: 8 }} busy={busy}
              disabled={!pt || !lines.length || invalidLines || !!discountError || (discountPaise > 0 && discountReason.trim().length < 3)} onClick={save}>Create bill</Button>
          </div>
        </div>
      </div>
    </>
  );
}

export function BillDetailPage({ id, print }: { id: string; print: boolean }) {
  const { can } = useAuth();
  const toast = useToast();
  const bill = useAsync(() => api.get<Bill>(`/bills/${id}`), [id]);
  const settings = useAsync(() => api.get<Settings>("/settings"), []);
  const [paying, setPaying] = useState<"PAYMENT" | "REFUND" | null>(null);
  const [cancelling, setCancelling] = useState(false);
  useEffect(() => { if (print && bill.data && settings.data) setTimeout(() => window.print(), 300); }, [print, bill.data, settings.data]);
  if (bill.loading && !bill.data) return <Loading />;
  if (bill.error) return <ErrorBanner error={bill.error} />;
  const b = bill.data!;
  const s = settings.data;
  const net = b.paidPaise - b.refundedPaise;

  return (
    <>
      <PageHeader title={`Invoice ${b.invoiceNumber}`} subtitle={<><StatusBadge status={b.status} /> &nbsp; {b.patientName}</>}
        actions={
          <>
            <Button onClick={() => window.print()}><IconPrinter /> Print invoice</Button>
            {b.balancePaise > 0 && b.status !== "CANCELLED" && can("billing.manage") && <Button variant="primary" onClick={() => setPaying("PAYMENT")}>Record payment</Button>}
            {net > 0 && can("billing.refund") && <Button variant="danger" onClick={() => setPaying("REFUND")}>Refund</Button>}
            {b.paidPaise === 0 && b.status !== "CANCELLED" && can("billing.refund") && <Button variant="danger" onClick={() => setCancelling(true)}>Cancel bill</Button>}
          </>
        } />
      {b.status === "CANCELLED" && <Banner kind="warn">This bill was cancelled: {b.cancelledReason}</Banner>}
      <div className="split">
        <article className="print-doc">
          <header>
            <div>
              <div className="clinic-name">{s?.clinicName}</div>
              <div className="small">{[s?.address, s?.city, s?.state, s?.pincode].filter(Boolean).join(", ")}</div>
              <div className="small">{s?.phone && `Phone ${s.phone}`}{s?.gstin && `   GSTIN ${s.gstin}`}</div>
            </div>
            <div className="right small">
              <strong style={{ fontSize: "1rem" }}>Invoice</strong>
              <div className="tabular">{b.invoiceNumber}</div>
              <div>{dateTime(b.createdAt)}</div>
            </div>
          </header>
          <p className="small"><strong>Billed to:</strong> {b.patientName} ({b.patientNumber}), {b.patientMobile}</p>
          <table>
            <thead><tr><th>#</th><th>Description</th><th className="right">Qty</th><th className="right">Rate</th><th className="right">Discount</th><th className="right">GST</th><th className="right">Amount</th></tr></thead>
            <tbody>
              {b.items?.map((it, i) => (
                <tr key={it.id}>
                  <td>{i + 1}</td><td>{it.description}</td><td className="right">{it.quantity}</td><td className="right num">{rupees(it.unitPricePaise)}</td>
                  <td className="right num">{it.lineDiscountPaise ? rupees(it.lineDiscountPaise) : "—"}</td>
                  <td className="right num">{it.taxRateBp ? `${rupees(it.lineTaxPaise)} (${it.taxRateBp / 100}%)` : "—"}</td>
                  <td className="right num">{rupees(it.lineTotalPaise)}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <table style={{ width: "50%", marginLeft: "auto" }}>
            <tbody>
              <tr><td>Subtotal</td><td className="right num">{rupees(b.subtotalPaise)}</td></tr>
              {b.discountPaise > 0 && <tr><td>Discount{b.discountReason ? ` (${b.discountReason})` : ""}</td><td className="right num">− {rupees(b.discountPaise)}</td></tr>}
              {b.taxPaise > 0 && <tr><td>GST</td><td className="right num">{rupees(b.taxPaise)}</td></tr>}
              <tr><td><strong>Total</strong></td><td className="right num"><strong>{rupees(b.totalPaise)}</strong></td></tr>
              <tr><td>Paid</td><td className="right num">{rupees(b.paidPaise)}</td></tr>
              {b.refundedPaise > 0 && <tr><td>Refunded</td><td className="right num">− {rupees(b.refundedPaise)}</td></tr>}
              <tr><td><strong>Balance due</strong></td><td className="right num"><strong>{rupees(b.balancePaise)}</strong></td></tr>
            </tbody>
          </table>
          {b.notes && <p className="small">{b.notes}</p>}
          <footer>This is a computer-generated invoice from ClinicFlow (proof of concept). Prepared by {b.createdBy}.</footer>
        </article>
        <div className="panel no-print">
          <div className="panel-head"><h3>Payments</h3></div>
          <div className="panel-body">
            {!b.payments?.length ? <p className="small muted" style={{ margin: 0 }}>No payments recorded.</p> : (
              <ul style={{ listStyle: "none", padding: 0, margin: 0 }} className="stack">
                {b.payments.map((p) => (
                  <li key={p.id}>
                    <div className="row" style={{ justifyContent: "space-between" }}>
                      <strong className="tabular" style={{ color: p.paymentType === "REFUND" ? "var(--red)" : undefined }}>{p.paymentType === "REFUND" ? "− " : ""}{rupees(p.amountPaise)}</strong>
                      <span className="badge plain b-grey">{humanize(p.method)}</span>
                    </div>
                    <div className="small muted">{p.paymentType === "REFUND" ? "Refund" : "Payment"}, {dateTime(p.receivedAt)} by {p.receivedBy}</div>
                    {p.reference && <div className="small">Ref {p.reference}</div>}
                    {p.notes && <div className="small">{p.notes}</div>}
                  </li>
                ))}
              </ul>
            )}
            <p className="small" style={{ marginTop: 14, marginBottom: 0 }}><a href={`#/patients/${b.patientId}?tab=billing`}>Patient's billing history</a></p>
          </div>
        </div>
      </div>
      {paying && <PaymentModal bill={b} kind={paying} onClose={() => setPaying(null)} onDone={(nb) => { bill.setData(nb); setPaying(null); toast(paying === "REFUND" ? "Refund recorded" : nb.status === "PAID" ? "Bill fully paid" : "Payment recorded"); }} />}
      {cancelling && <CancelBillModal bill={b} onClose={() => setCancelling(false)} onDone={(nb) => { bill.setData(nb); setCancelling(false); toast("Bill cancelled"); }} />}
    </>
  );
}

function PaymentModal({ bill, kind, onClose, onDone }: { bill: Bill; kind: "PAYMENT" | "REFUND"; onClose: () => void; onDone: (b: Bill) => void }) {
  const max = kind === "PAYMENT" ? bill.balancePaise : bill.paidPaise - bill.refundedPaise;
  const [amount, setAmount] = useState((max / 100).toString());
  const [method, setMethod] = useState("CASH");
  const [reference, setReference] = useState("");
  const [notes, setNotes] = useState("");
  const [err, setErr] = useState<ApiError | null>(null);
  const paise = toPaise(amount);
  const amountErr = paise === null || paise <= 0 ? "Enter an amount" : paise > max ? `Maximum ${rupees(max)}` : undefined;
  const save = async () => {
    try {
      onDone(await api.post<Bill>(`/bills/${bill.id}/${kind === "PAYMENT" ? "payments" : "refunds"}`, { amountPaise: paise, method, reference: reference || undefined, notes: notes || undefined }));
    } catch (e) { setErr(e as ApiError); }
  };
  return (
    <Modal title={kind === "PAYMENT" ? "Record payment" : "Record refund"} onClose={onClose}
      footer={<><Button onClick={onClose}>Cancel</Button><Button variant={kind === "PAYMENT" ? "primary" : "danger"} disabled={!!amountErr || (kind === "REFUND" && notes.trim().length < 3)} onClick={save}>{kind === "PAYMENT" ? "Record payment" : "Record refund"}</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <div className="grid g2">
        <TextField label="Amount (₹)" value={amount} onChange={setAmount} inputMode="decimal" error={amountErr} hint={`${kind === "PAYMENT" ? "Balance due" : "Refundable"}: ${rupees(max)}`} />
        <SelectField label="Method" value={method} onChange={setMethod} options={["CASH", "UPI", "CARD", "OTHER"]} />
        <TextField label={method === "UPI" ? "UPI transaction ID" : method === "CARD" ? "Card approval code" : "Reference"} value={reference} onChange={setReference} className="span-all" />
        <TextField label={kind === "REFUND" ? "Reason for refund" : "Notes"} value={notes} onChange={setNotes} className="span-all" required={kind === "REFUND"} />
      </div>
    </Modal>
  );
}

function CancelBillModal({ bill, onClose, onDone }: { bill: Bill; onClose: () => void; onDone: (b: Bill) => void }) {
  const [reason, setReason] = useState("");
  const [err, setErr] = useState<ApiError | null>(null);
  return (
    <Modal title={`Cancel ${bill.invoiceNumber}?`} onClose={onClose}
      footer={<><Button onClick={onClose}>Keep bill</Button><Button variant="danger" disabled={reason.trim().length < 3} onClick={async () => { try { onDone(await api.post<Bill>(`/bills/${bill.id}/cancel`, { reason })); } catch (e) { setErr(e as ApiError); } }}>Cancel bill</Button></>}>
      {err && <Banner kind="error">{err.message}</Banner>}
      <p className="small">The invoice number stays reserved and the bill remains visible as cancelled.</p>
      <TextField label="Reason" required value={reason} onChange={setReason} />
    </Modal>
  );
}


