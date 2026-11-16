// Handwriting pad for Apple Pencil / stylus (also works with mouse and finger).
//
// iPad Safari specifics handled here:
//  - Pointer Events with getCoalescedEvents() for full Pencil sampling, pressure
//    mapped to line width, two canvases (committed ink + live stroke) so drawing
//    never re-renders the whole page while the pen moves.
//  - touch-action:none plus non-passive touch handlers stop Safari from scrolling,
//    zooming, showing the magnifier or selecting text while writing.
//  - Palm rejection: once a Pencil is seen, finger touches no longer draw; one
//    finger scrolls the page instead (two fingers pinch-zoom is disabled on the pad).
import { useCallback, useEffect, useImperativeHandle, useRef, useState, forwardRef } from "react";
import { api, ApiError } from "../../lib/api";
import { Ink, InkPoint, PAGE_H, PAGE_W, PEN_COLOURS, PEN_SIZES, Stroke, drawInk, drawSegment, emptyInk, normalisePressure, renderPng, simplify, strokeHit } from "../../lib/ink";
import { Banner, Button, Modal, useToast } from "../../components/ui";

export interface PadPage { pageNo: number; strokeCount: number; version: number; hasImage: boolean }
export interface PadHandle { flush: () => Promise<boolean> }

type SaveState = "idle" | "pending" | "saving" | "saved" | "error";
type Action = { kind: "add"; stroke: Stroke } | { kind: "erase"; strokes: { stroke: Stroke; index: number }[] } | { kind: "clear"; strokes: Stroke[] };

export const HandwritingPad = forwardRef<PadHandle, {
  consultationId: string; initialPages: PadPage[]; onSaved?: (pages: PadPage[]) => void; onAnalyse: () => void; analysing?: boolean;
}>(function HandwritingPad({ consultationId, initialPages, onSaved, onAnalyse, analysing }, ref) {
  const toast = useToast();
  const wrapRef = useRef<HTMLDivElement>(null);
  const baseRef = useRef<HTMLCanvasElement>(null);
  const liveRef = useRef<HTMLCanvasElement>(null);
  const [pages, setPages] = useState<PadPage[]>(initialPages.length ? initialPages : [{ pageNo: 1, strokeCount: 0, version: 0, hasImage: false }]);
  const pagesRef = useRef(pages);
  pagesRef.current = pages;
  const [pageNo, setPageNo] = useState(1);
  const [tool, setTool] = useState<"pen" | "eraser">("pen");
  const [colour, setColour] = useState(PEN_COLOURS[0].id);
  const [size, setSize] = useState(PEN_SIZES[1].id);
  const [pencilOnly, setPencilOnly] = useState(false);
  const [saveState, setSaveState] = useState<SaveState>("idle");
  const [saveError, setSaveError] = useState<string | null>(null);
  const [confirmClear, setConfirmClear] = useState(false);
  const [loadingPage, setLoadingPage] = useState(false);
  const [, force] = useState(0);

  // Mutable drawing state (kept out of React state so pointer moves never re-render)
  const ink = useRef<Map<number, Ink>>(new Map());
  const versions = useRef<Map<number, number>>(new Map(initialPages.map((p) => [p.pageNo, p.version])));
  const undo = useRef<Map<number, Action[]>>(new Map());
  const redo = useRef<Map<number, Action[]>>(new Map());
  const dirtyPages = useRef<Set<number>>(new Set());
  const scaleRef = useRef(1);
  const current = useRef<{ stroke: Stroke; id: number; t0: number } | null>(null);
  const erasing = useRef<{ id: number; removed: { stroke: Stroke; index: number }[] } | null>(null);
  const fingerScroll = useRef<{ id: number; y: number } | null>(null);
  const saveTimer = useRef<number | undefined>(undefined);
  const savingPromise = useRef<Promise<boolean> | null>(null);
  const pencilSeen = useRef(false);

  const inkOf = (n: number) => { if (!ink.current.has(n)) ink.current.set(n, emptyInk()); return ink.current.get(n)!; };
  const stack = (m: Map<number, Action[]>, n: number) => { if (!m.has(n)) m.set(n, []); return m.get(n)!; };

  // ---- canvas sizing (device-pixel-ratio aware)
  const layout = useCallback(() => {
    const wrap = wrapRef.current, base = baseRef.current, live = liveRef.current;
    if (!wrap || !base || !live) return;
    const cssW = wrap.clientWidth;
    const cssH = Math.round((cssW * PAGE_H) / PAGE_W);
    const dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    for (const c of [base, live]) {
      c.style.width = `${cssW}px`;
      c.style.height = `${cssH}px`;
      c.width = Math.round(cssW * dpr);
      c.height = Math.round(cssH * dpr);
    }
    wrap.style.height = `${cssH}px`;
    scaleRef.current = (cssW * dpr) / PAGE_W;
    redrawBase();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const redrawBase = () => {
    const c = baseRef.current;
    if (!c) return;
    const ctx = c.getContext("2d")!;
    ctx.clearRect(0, 0, c.width, c.height);
    drawInk(ctx, inkOf(pageRef.current), scaleRef.current);
  };
  const pageRef = useRef(1);
  pageRef.current = pageNo;

  useEffect(() => {
    layout();
    const ro = new ResizeObserver(() => layout());
    if (wrapRef.current) ro.observe(wrapRef.current);
    return () => ro.disconnect();
  }, [layout]);

  // ---- load a page's ink from the server the first time it is shown
  useEffect(() => {
    let cancelled = false;
    const meta = pages.find((p) => p.pageNo === pageNo);
    if (ink.current.has(pageNo) || !meta || meta.version === 0) { redrawBase(); return; }
    setLoadingPage(true);
    api.get<Ink & { version: number }>(`/consultations/${consultationId}/handwriting/pages/${pageNo}/ink`)
      .then((d) => {
        if (cancelled) return;
        ink.current.set(pageNo, { v: 1, strokes: d.strokes ?? [] });
        versions.current.set(pageNo, d.version);
        redrawBase();
      })
      .catch(() => { if (!cancelled) toast("Could not load this page. Check the connection and reopen the tab."); })
      .finally(() => !cancelled && setLoadingPage(false));
    return () => { cancelled = true; };
  }, [pageNo, consultationId]); // eslint-disable-line react-hooks/exhaustive-deps

  // ---- saving (debounced autosave, one request at a time per pad)
  const savePage = async (n: number): Promise<boolean> => {
    const pageInk = inkOf(n);
    const body: Record<string, unknown> = { width: PAGE_W, height: PAGE_H, ink: pageInk, expectedVersion: versions.current.get(n) || undefined };
    if (pageInk.strokes.length) body.imageBase64 = renderPng(pageInk);
    const r = await api.put<{ version: number; strokeCount: number }>(`/consultations/${consultationId}/handwriting/pages/${n}`, body);
    versions.current.set(n, r.version);
    // Tell the parent after this render, not from inside the state updater
    // (React forbids updating another component while rendering this one).
    const next = pagesRef.current.map((p) => (p.pageNo === n ? { ...p, version: r.version, strokeCount: r.strokeCount, hasImage: r.strokeCount > 0 } : p));
    pagesRef.current = next;
    setPages(next);
    onSaved?.(next);
    return true;
  };
  const flush = useCallback(async (): Promise<boolean> => {
    window.clearTimeout(saveTimer.current);
    if (savingPromise.current) await savingPromise.current;
    if (!dirtyPages.current.size) return true;
    setSaveState("saving");
    const run = (async () => {
      try {
        for (const n of Array.from(dirtyPages.current)) {
          await savePage(n);
          dirtyPages.current.delete(n);
        }
        setSaveState("saved"); setSaveError(null);
        return true;
      } catch (e) {
        const err = e as ApiError;
        setSaveState("error");
        setSaveError(err.code === "VERSION_CONFLICT" ? "This page was changed in another window. Reload the consultation before continuing." : err.message);
        return false;
      } finally { savingPromise.current = null; }
    })();
    savingPromise.current = run;
    return run;
  }, [consultationId]); // eslint-disable-line react-hooks/exhaustive-deps
  useImperativeHandle(ref, () => ({ flush }), [flush]);

  const markDirty = () => {
    dirtyPages.current.add(pageRef.current);
    setSaveState("pending");
    window.clearTimeout(saveTimer.current);
    saveTimer.current = window.setTimeout(() => { flush(); }, 1500);
    force((x) => x + 1);
  };
  useEffect(() => {
    const h = (e: BeforeUnloadEvent) => { if (dirtyPages.current.size) e.preventDefault(); };
    window.addEventListener("beforeunload", h);
    return () => { window.removeEventListener("beforeunload", h); window.clearTimeout(saveTimer.current); if (dirtyPages.current.size) flush(); };
  }, [flush]);

  // ---- pointer handling
  const toPage = (e: PointerEvent) => {
    const rect = liveRef.current!.getBoundingClientRect();
    const k = PAGE_W / rect.width;
    return { x: (e.clientX - rect.left) * k, y: (e.clientY - rect.top) * k };
  };
  const eraseAt = (x: number, y: number) => {
    const page = inkOf(pageRef.current);
    const removed: { stroke: Stroke; index: number }[] = [];
    for (let i = page.strokes.length - 1; i >= 0; i--) {
      if (strokeHit(page.strokes[i], x, y, 10)) { removed.push({ stroke: page.strokes[i], index: i }); page.strokes.splice(i, 1); }
    }
    if (removed.length) { erasing.current!.removed.push(...removed); redrawBase(); }
  };

  useEffect(() => {
    const live = liveRef.current!;
    const lctx = live.getContext("2d")!;
    const down = (e: PointerEvent) => {
      if (e.pointerType === "pen" && !pencilSeen.current) {
        pencilSeen.current = true;
        setPencilOnly(true);
      }
      if (e.pointerType === "touch" && (pencilSeen.current || current.current)) {
        // Palm / finger: scroll instead of drawing
        fingerScroll.current = { id: e.pointerId, y: e.clientY };
        return;
      }
      if (current.current || erasing.current) return;  // one stroke at a time
      e.preventDefault();
      try { live.setPointerCapture(e.pointerId); } catch { /* capture is best-effort */ }
      const { x, y } = toPage(e);
      // Pen eraser end / barrel eraser button (buttons bit 32) also erases
      if (tool === "eraser" || (e.buttons & 32) === 32) {
        erasing.current = { id: e.pointerId, removed: [] };
        eraseAt(x, y);
        return;
      }
      current.current = { id: e.pointerId, t0: e.timeStamp, stroke: { t: "pen", c: colour, s: size, p: [[x, y, normalisePressure(e.pressure, e.pointerType), 0]] } };
    };
    const move = (e: PointerEvent) => {
      const fs = fingerScroll.current;
      if (fs && fs.id === e.pointerId) { window.scrollBy(0, fs.y - e.clientY); fs.y = e.clientY; return; }
      if (erasing.current && erasing.current.id === e.pointerId) {
        const evs = (e.getCoalescedEvents?.() ?? []).length ? e.getCoalescedEvents() : [e];
        for (const ev of evs) { const { x, y } = toPage(ev); eraseAt(x, y); }
        return;
      }
      const cur = current.current;
      if (!cur || cur.id !== e.pointerId) return;
      e.preventDefault();
      const evs = (e.getCoalescedEvents?.() ?? []).length ? e.getCoalescedEvents() : [e];
      for (const ev of evs) {
        const { x, y } = toPage(ev);
        const last = cur.stroke.p[cur.stroke.p.length - 1];
        if (Math.hypot(x - last[0], y - last[1]) < 0.35) continue;
        const pt: InkPoint = [x, y, normalisePressure(ev.pressure, ev.pointerType), ev.timeStamp - cur.t0];
        cur.stroke.p.push(pt);
        drawSegment(lctx, cur.stroke, cur.stroke.p.length - 1, scaleRef.current);
      }
    };
    const up = (e: PointerEvent) => {
      if (fingerScroll.current?.id === e.pointerId) { fingerScroll.current = null; return; }
      if (erasing.current && erasing.current.id === e.pointerId) {
        const removed = erasing.current.removed;
        erasing.current = null;
        if (removed.length) {
          stack(undo.current, pageRef.current).push({ kind: "erase", strokes: removed });
          stack(redo.current, pageRef.current).length = 0;
          markDirty();
        }
        return;
      }
      const cur = current.current;
      if (!cur || cur.id !== e.pointerId) return;
      current.current = null;
      const stroke = { ...cur.stroke, p: simplify(cur.stroke.p) };
      inkOf(pageRef.current).strokes.push(stroke);
      lctx.clearRect(0, 0, live.width, live.height);
      const bctx = baseRef.current!.getContext("2d")!;
      drawInk(bctx, { v: 1, strokes: [stroke] }, scaleRef.current);
      stack(undo.current, pageRef.current).push({ kind: "add", stroke });
      stack(redo.current, pageRef.current).length = 0;
      markDirty();
    };
    // iOS Safari: stop the page from scrolling / zooming / showing callouts under the pen
    const stopTouch = (e: TouchEvent) => { if (e.cancelable) e.preventDefault(); };
    live.addEventListener("pointerdown", down);
    live.addEventListener("pointermove", move);
    live.addEventListener("pointerup", up);
    live.addEventListener("pointercancel", up);
    live.addEventListener("touchstart", stopTouch, { passive: false });
    live.addEventListener("touchmove", stopTouch, { passive: false });
    return () => {
      live.removeEventListener("pointerdown", down);
      live.removeEventListener("pointermove", move);
      live.removeEventListener("pointerup", up);
      live.removeEventListener("pointercancel", up);
      live.removeEventListener("touchstart", stopTouch);
      live.removeEventListener("touchmove", stopTouch);
    };
  }, [tool, colour, size]); // eslint-disable-line react-hooks/exhaustive-deps

  // ---- undo / redo / clear
  const applyUndo = () => {
    const a = stack(undo.current, pageNo).pop();
    if (!a) return;
    const page = inkOf(pageNo);
    if (a.kind === "add") page.strokes.splice(page.strokes.lastIndexOf(a.stroke), 1);
    else if (a.kind === "erase") for (const r of [...a.strokes].reverse()) page.strokes.splice(Math.min(r.index, page.strokes.length), 0, r.stroke);
    else page.strokes = [...a.strokes];
    stack(redo.current, pageNo).push(a);
    redrawBase(); markDirty();
  };
  const applyRedo = () => {
    const a = stack(redo.current, pageNo).pop();
    if (!a) return;
    const page = inkOf(pageNo);
    if (a.kind === "add") page.strokes.push(a.stroke);
    else if (a.kind === "erase") for (const r of a.strokes) { const i = page.strokes.indexOf(r.stroke); if (i >= 0) page.strokes.splice(i, 1); }
    else page.strokes = [];
    stack(undo.current, pageNo).push(a);
    redrawBase(); markDirty();
  };
  const clearPage = () => {
    const page = inkOf(pageNo);
    if (!page.strokes.length) { setConfirmClear(false); return; }
    stack(undo.current, pageNo).push({ kind: "clear", strokes: [...page.strokes] });
    stack(redo.current, pageNo).length = 0;
    page.strokes = [];
    redrawBase(); markDirty(); setConfirmClear(false);
  };
  useEffect(() => {
    const k = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "z") { e.preventDefault(); if (e.shiftKey) applyRedo(); else applyUndo(); }
    };
    window.addEventListener("keydown", k);
    return () => window.removeEventListener("keydown", k);
  });

  const goTo = async (n: number) => { await flush(); setPageNo(n); };
  const addPage = async () => {
    const n = Math.max(...pages.map((p) => p.pageNo)) + 1;
    if (n > 20) { toast("A consultation can have up to 20 pages"); return; }
    await flush();
    setPages([...pages, { pageNo: n, strokeCount: 0, version: 0, hasImage: false }]);
    setPageNo(n);
  };

  const strokes = inkOf(pageNo).strokes.length;
  const canUndo = (undo.current.get(pageNo)?.length ?? 0) > 0;
  const canRedo = (redo.current.get(pageNo)?.length ?? 0) > 0;
  const saveLabel = { idle: "", pending: "Unsaved changes", saving: "Saving…", saved: "Saved", error: "Not saved" }[saveState];

  return (
    <div className="hw-pad">
      <div className="hw-toolbar" role="toolbar" aria-label="Writing tools">
        <div className="hw-group" role="group" aria-label="Tool">
          <button type="button" className="hw-tool" aria-pressed={tool === "pen"} onClick={() => setTool("pen")} title="Pen">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 20l4-1 10-10-3-3L5 16l-1 4z" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinejoin="round" /></svg>Pen
          </button>
          <button type="button" className="hw-tool" aria-pressed={tool === "eraser"} onClick={() => setTool("eraser")} title="Eraser (removes whole strokes)">
            <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 20h11M4 15l8-8 6 6-6 6H8l-4-4z" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinejoin="round" /></svg>Eraser
          </button>
        </div>
        <div className="hw-group" role="group" aria-label="Colour">
          {PEN_COLOURS.map((c) => (
            <button type="button" key={c.id} className="hw-swatch" aria-pressed={colour === c.id} aria-label={c.label} title={c.label}
              style={{ ["--sw" as string]: c.id }} onClick={() => { setColour(c.id); setTool("pen"); }} />
          ))}
        </div>
        <div className="hw-group" role="group" aria-label="Thickness">
          {PEN_SIZES.map((s) => (
            <button type="button" key={s.id} className="hw-size" aria-pressed={size === s.id} aria-label={s.label} title={s.label}
              onClick={() => { setSize(s.id); setTool("pen"); }}><span style={{ width: s.id * 3, height: s.id * 3 }} /></button>
          ))}
        </div>
        <div className="hw-group">
          <button type="button" className="hw-tool" onClick={applyUndo} disabled={!canUndo} aria-label="Undo">↶</button>
          <button type="button" className="hw-tool" onClick={applyRedo} disabled={!canRedo} aria-label="Redo">↷</button>
          <button type="button" className="hw-tool" onClick={() => setConfirmClear(true)} disabled={!strokes}>Clear page</button>
        </div>
        <div className="hw-group hw-pages" role="group" aria-label="Pages">
          <button type="button" className="hw-tool" disabled={pageNo <= 1} onClick={() => goTo(pageNo - 1)} aria-label="Previous page">‹</button>
          <span className="small tabular" aria-live="polite">Page {pageNo} of {pages.length}</span>
          <button type="button" className="hw-tool" disabled={pageNo >= pages.length} onClick={() => goTo(pageNo + 1)} aria-label="Next page">›</button>
          <button type="button" className="hw-tool" onClick={addPage}>+ Page</button>
        </div>
        <span className={`hw-save small ${saveState}`} aria-live="polite">{saveLabel}</span>
        <Button variant="primary" busy={analysing} onClick={async () => { if (await flush()) onAnalyse(); }}
          disabled={!pages.some((p) => p.strokeCount > 0) && !strokes}>Analyse handwriting</Button>
      </div>
      {saveError && <Banner kind="error">{saveError} <button className="btn btn-sm" onClick={() => flush()}>Retry</button></Banner>}
      <div className="hw-hint small muted">
        {pencilOnly ? "Apple Pencil detected: your palm and fingers won't draw. Swipe with one finger to scroll."
          : "Write with Apple Pencil or a stylus. A finger or mouse also works until a Pencil is used."}
      </div>
      <div ref={wrapRef} className="hw-sheet" aria-label={`Handwriting page ${pageNo}`}>
        <canvas ref={baseRef} className="hw-canvas" aria-hidden="true" />
        <canvas ref={liveRef} className="hw-canvas hw-live" role="img" aria-label="Writing surface" />
        {loadingPage && <div className="hw-loading"><span className="spinner" /> Loading page…</div>}
      </div>
      {confirmClear && (
        <Modal title="Clear this page?" onClose={() => setConfirmClear(false)}
          footer={<><Button onClick={() => setConfirmClear(false)}>Cancel</Button><Button variant="danger" onClick={clearPage}>Clear page</Button></>}>
          <p>All handwriting on page {pageNo} will be removed. You can undo this until you leave the page.</p>
        </Modal>
      )}
    </div>
  );
});
