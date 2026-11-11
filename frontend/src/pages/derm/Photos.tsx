// Clinical photographs: consent is required before capture; images are fetched
// with the session token (never via a public URL) and every view is audited.
import { useEffect, useRef, useState } from "react";
import { api, ApiError } from "../../lib/api";
import { useAuth } from "../../lib/auth";
import { DermVocabulary, Photo, labelFor, resizeImage } from "../../lib/derm";
import { dateTime, humanize } from "../../lib/format";
import { Banner, Button, Modal, SelectField, TextField, useAsync, useToast } from "../../components/ui";

function Thumb({ photo, onOpen }: { photo: Photo; onOpen: (url: string) => void }) {
  const [url, setUrl] = useState<string | null>(null);
  const [failed, setFailed] = useState(false);
  useEffect(() => {
    let u: string | null = null;
    api.blobUrl(`/photos/${photo.id}/image`).then((x) => { u = x; setUrl(x); }).catch(() => setFailed(true));
    return () => { if (u) URL.revokeObjectURL(u); };
  }, [photo.id]);
  if (failed) return <div className="photo-thumb muted small">Unavailable</div>;
  if (!url) return <div className="photo-thumb"><span className="spinner" /></div>;
  return (
    <button className="photo-thumb" onClick={() => onOpen(url)} aria-label={`Open photograph ${photo.caption ?? ""}`}>
      <img src={url} alt={photo.caption ?? "Clinical photograph"} />
    </button>
  );
}

export function PhotosPanel({ consultationId, editable, vocab }: { consultationId: string; editable: boolean; vocab: DermVocabulary }) {
  const { can } = useAuth();
  const toast = useToast();
  const list = useAsync(() => (can("photo.read") ? api.get<Photo[]>(`/consultations/${consultationId}/photos`) : Promise.resolve([] as Photo[])), [consultationId]);
  const [adding, setAdding] = useState(false);
  const [viewer, setViewer] = useState<{ url: string; photo: Photo } | null>(null);
  const [showHidden, setShowHidden] = useState(false);
  if (!can("photo.read")) return null;
  const photos = (list.data ?? []).filter((p) => showHidden || p.status === "ACTIVE");
  const hiddenCount = (list.data ?? []).filter((p) => p.status !== "ACTIVE").length;

  return (
    <section className="panel">
      <div className="panel-head">
        <h3>Clinical photographs</h3>
        <div className="row">
          {hiddenCount > 0 && <label className="small"><input type="checkbox" checked={showHidden} onChange={(e) => setShowHidden(e.target.checked)} /> Show {hiddenCount} entered in error</label>}
          {editable && can("photo.write") && <Button size="sm" onClick={() => setAdding(true)}>Add photograph</Button>}
        </div>
      </div>
      <div className="panel-body">
        {list.loading && !list.data ? <span className="small muted">Loading…</span> : photos.length === 0 ? (
          <p className="small muted" style={{ margin: 0 }}>No photographs. Photographs need the patient's consent and are stored encrypted.</p>
        ) : (
          <div className="photo-grid">
            {photos.map((p) => (
              <figure key={p.id} className={p.status !== "ACTIVE" ? "photo-error" : ""}>
                <Thumb photo={p} onOpen={(url) => setViewer({ url, photo: p })} />
                <figcaption className="small">
                  <strong>{labelFor(vocab.bodySites, p.bodySite) || "Site not recorded"}</strong>{p.caption ? ` · ${p.caption}` : ""}
                  <div className="muted">{dateTime(p.capturedAt)} · {humanize(p.consentScope)}</div>
                  {p.status !== "ACTIVE" && <div className="muted">Entered in error: {p.statusReason}</div>}
                </figcaption>
              </figure>
            ))}
          </div>
        )}
      </div>
      {adding && <AddPhotoModal consultationId={consultationId} vocab={vocab} onClose={() => setAdding(false)} onSaved={() => { setAdding(false); list.reload(); toast("Photograph saved"); }} />}
      {viewer && (
        <Modal wide title={labelFor(vocab.bodySites, viewer.photo.bodySite) || "Clinical photograph"} onClose={() => setViewer(null)}
          footer={editable && can("photo.write") && viewer.photo.status === "ACTIVE" ? (
            <MarkError photo={viewer.photo} onDone={() => { setViewer(null); list.reload(); }} />) : undefined}>
          <img src={viewer.url} alt={viewer.photo.caption ?? "Clinical photograph"} style={{ maxWidth: "100%", maxHeight: "70vh", display: "block", margin: "0 auto" }} />
          <p className="small muted" style={{ marginTop: 8 }}>Captured by {viewer.photo.capturedBy}, {dateTime(viewer.photo.capturedAt)}. Consent: {humanize(viewer.photo.consentScope)}. This view is recorded in the audit log.</p>
        </Modal>
      )}
    </section>
  );
}

function MarkError({ photo, onDone }: { photo: Photo; onDone: () => void }) {
  const [reason, setReason] = useState("");
  const [busy, setBusy] = useState(false);
  return (
    <div className="row" style={{ width: "100%" }}>
      <input className="input" style={{ flex: 1 }} placeholder="Reason, e.g. wrong patient / duplicate" value={reason} onChange={(e) => setReason(e.target.value)} aria-label="Reason" />
      <Button variant="danger" busy={busy} disabled={reason.trim().length < 3} onClick={async () => {
        setBusy(true);
        try { await api.post(`/photos/${photo.id}/entered-in-error`, { reason }); onDone(); } finally { setBusy(false); }
      }}>Mark entered in error</Button>
    </div>
  );
}

function AddPhotoModal({ consultationId, vocab, onClose, onSaved }: { consultationId: string; vocab: DermVocabulary; onClose: () => void; onSaved: () => void }) {
  const fileRef = useRef<HTMLInputElement>(null);
  const [data, setData] = useState<string | null>(null);
  const [site, setSite] = useState("");
  const [caption, setCaption] = useState("");
  const [consent, setConsent] = useState(false);
  const [teaching, setTeaching] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const pick = async (f?: File) => {
    if (!f) return;
    setErr(null);
    if (!/^image\/(jpeg|png|webp)$/.test(f.type)) { setErr("Choose a JPEG, PNG or WebP image"); return; }
    try { setData(await resizeImage(f)); } catch (e) { setErr((e as Error).message); }
  };
  const save = async () => {
    setBusy(true); setErr(null);
    try {
      await api.post(`/consultations/${consultationId}/photos`, { dataBase64: data, bodySite: site || null, caption: caption || null,
        consentObtained: consent, consentScope: teaching ? "CLINICAL_RECORD_AND_TEACHING" : "CLINICAL_RECORD_ONLY" });
      onSaved();
    } catch (e) { setErr((e as ApiError).message); } finally { setBusy(false); }
  };
  return (
    <Modal title="Add clinical photograph" onClose={onClose}
      footer={<><Button onClick={onClose}>Cancel</Button><Button variant="primary" busy={busy} disabled={!data || !consent} onClick={save}>Save photograph</Button></>}>
      <div className="stack">
        {err && <Banner kind="error">{err}</Banner>}
        <label className="consent-box">
          <input type="checkbox" checked={consent} onChange={(e) => setConsent(e.target.checked)} />
          <span><strong>The patient (or guardian) has consented</strong> to this photograph being taken and stored in their clinical record.</span>
        </label>
        <label className="small"><input type="checkbox" checked={teaching} onChange={(e) => setTeaching(e.target.checked)} disabled={!consent} /> Consent also covers de-identified use for teaching</label>
        <input ref={fileRef} type="file" accept="image/jpeg,image/png,image/webp" capture="environment" onChange={(e) => pick(e.target.files?.[0])} disabled={!consent} />
        {data && <img src={data} alt="Preview" style={{ maxWidth: "100%", maxHeight: 260, objectFit: "contain", border: "1px solid var(--rule)", borderRadius: 6 }} />}
        <div className="grid g2">
          <SelectField label="Body site" value={site} onChange={setSite} options={vocab.bodySites.map((b) => ({ value: b.code, label: b.label }))} placeholder="Select" />
          <TextField label="Caption" value={caption} onChange={setCaption} maxLength={200} placeholder="e.g. Left forearm, day 1" />
        </div>
        <p className="small muted" style={{ margin: 0 }}>Images are resized on this device, encrypted on the server, and never deleted (mistakes are marked entered-in-error).</p>
      </div>
    </Modal>
  );
}
