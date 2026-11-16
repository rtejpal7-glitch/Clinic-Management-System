# Handwritten consultation notes (v0.3)

Doctors can write the consultation by hand on an iPad with Apple Pencil, the way they would in a paper case sheet or Goodnotes. ClinicFlow keeps the handwriting, turns it into text the doctor checks and corrects, and offers structured fields the doctor ticks before anything goes into the consultation.

**Nothing is automatic.** Recognised text is a draft. Proposed findings, diagnoses and medicines are unticked or ticked suggestions that the doctor confirms. Diagnoses are added as *provisional*; medicines go into the *draft* prescription with "From handwritten notes - check dose". The consultation is only finalized when the doctor presses Finalize, as before.

## Workflow

1. **Start consultation.** From the queue or appointment, the doctor presses *Start consultation*. On a tablet (iPadOS reports 5 touch points) the **Handwritten notes** tab opens automatically. On a desktop it is the tab after *Notes*.
2. **Write.** Pen, three colours, three widths, stroke eraser, undo/redo, clear page, up to 20 A4 pages. Pages save automatically 1.5 s after the pen lifts (the toolbar shows *Saving… / Saved*). Two people editing the same page get a version conflict, not a silent overwrite.
3. **Analyse handwriting.** The pages are sent to the configured recognition provider (below). The review screen shows the **original handwriting on the left** and the **recognised text on the right**.
4. **Correct.** The doctor edits the text directly. Words that look like misread medical terms ("erythematus", "itcy") are shown as one-tap corrections. The proposed fields refresh as the text changes.
5. **Choose what to add.** Proposed fields are grouped as notes (complaint, history, examination, plan), assessment (problem type, duration, symptoms, affected areas, lesion types, colour, distribution, triggers), possible diagnoses (ICD-10) and medicines mentioned. Exact matches are pre-ticked; fuzzy matches, diagnoses and medicines start unticked. Items written as negatives ("no fever") are listed as absent and are not added as findings.
6. **Accept and add to consultation.** If the text changed since the fields were last proposed, ClinicFlow refreshes them and asks the doctor to check again. Otherwise the ticked items go into the consultation notes, the dermatology assessment, the diagnoses and the draft prescription, and the transcription is stored as **ACCEPTED** with the doctor's name and time.
7. **Decision support.** A banner offers *Open decision support*, which runs the existing dermatology panels (A/B/C) on the approved information. Suggestions there are still suggestions; the doctor decides.
8. **Record.** After finalization the pages are read-only (database trigger), and the patient's history shows "✍ N handwritten pages · doctor-approved transcription". The original page images and the approved transcription stay with the consultation.

The doctor can also skip recognition: type the text in the review box (or use iPadOS **Scribble**, which converts Apple Pencil writing in any text box), and the same extraction and approval steps apply.

## What is stored

| Table | Contents | Protection |
|---|---|---|
| `handwritten_note_pages` | Vector ink (points with pressure and time) and a PNG rendering per page, SHA-256 of the image, version | AES-256-GCM at rest; RLS by clinic; locked when the consultation is finalized; delete only while draft |
| `handwriting_transcriptions` | Provider, recognised text (machine output, immutable), doctor-corrected text, extraction result, accepted fields, status, reviewer and time | RLS by clinic; no deletes; immutable once ACCEPTED or DISCARDED; superseded pending drafts are marked DISCARDED |

Audit events: `HANDWRITING_SAVE` (with image hash), `HANDWRITING_VIEW`, `HANDWRITING_DELETE`, `HANDWRITING_RECOGNITION` (provider, whether data left ClinicFlow, error), `HANDWRITING_ACCEPT`, `HANDWRITING_DISCARD`. Only the treating doctor can write or accept, and only while the consultation is a draft.

## Recognition providers

Recognition is a pluggable port, like the ABDM gateway. Set it on the **backend** (`.env` for Docker):

| `HWR_PROVIDER` | What happens | Settings |
|---|---|---|
| `none` (default) | No recognition. *Analyse handwriting* opens the review screen with an empty text box for the doctor to type into. Nothing leaves ClinicFlow. | — |
| `google-vision` | Page PNGs are sent to Google Cloud Vision `DOCUMENT_TEXT_DETECTION` with the handwriting language hint. | `HWR_GOOGLE_API_KEY`, optional `HWR_GOOGLE_ENDPOINT` (e.g. a regional endpoint) |
| `http` | Page PNGs are sent to your own service (for example a self-hosted handwriting OCR model inside your network). | `HWR_HTTP_URL`, optional `HWR_HTTP_TOKEN` (sent as `Authorization: Bearer`), `HWR_HTTP_LABEL` (shown to doctors) |

Common: `HWR_TIMEOUT_SECONDS` (default 30), `HWR_CA_FILE` (extra CA bundle for a private service). TLS certificates and host names are always verified.

**HTTP contract** (one request per page):

```
POST {HWR_HTTP_URL}
Content-Type: application/json
Authorization: Bearer {HWR_HTTP_TOKEN}

{"imageBase64": "<PNG>", "mimeType": "image/png", "languageHints": ["en"], "purpose": "clinical-handwriting"}

200 OK
{"text": "c/o itchy wheals x 3 days ..."}
```

Any non-200 status or a missing `text` is shown to the doctor as "recognition failed", and they can type the text instead.

### Privacy before enabling an external provider

Page images contain identifiable health information. Before setting `google-vision` (or an `http` service run by a third party):

- Put a data processing agreement in place and record the provider as a processor under the DPDP Act 2023 and your clinic's privacy notice.
- Choose the data location deliberately (Google Vision offers regional endpoints such as `https://eu-vision.googleapis.com`; check current Google documentation for India options).
- Restrict the API key to the Vision API and to your backend's IP address.
- The doctor sees which provider is configured and whether it sends data outside ClinicFlow before pressing *Analyse*.

## Clinical text extraction

Extraction (`backend/src/derm/ClinicalTextExtractor.cpp`, engine `note-extract-1.0`) is **deterministic and rule-based**, not AI. It understands common case-sheet shorthand: section headings (c/o, h/o, o/e, imp, ddx, rx, plan), durations (x 3/7, 2/52, 6/12, "two weeks"), negation (no, nil, denies, -ve), query diagnoses (?, r/o, likely), and prescription shorthand (OD, BD, TDS, QID, HS, SOS, 1-0-1). It is tuned for dermatology vocabulary; terms outside its lexicon are left in the text and not turned into fields. It never invents a finding that is not in the text (unit test `extract_does_not_invent_findings`).

## Using it on an iPad

- Open ClinicFlow in Safari, then **Share → Add to Home Screen**. It then opens full screen without Safari's toolbars, which leaves more room to write.
- Rest your palm on the screen while writing: once the Pencil has touched the page, finger and palm touches no longer draw. Dragging a finger on the page scrolls it instead.
- Double-tap the Pencil (2nd generation / Pro) is not mapped; use the Eraser button. A stylus with an eraser end is recognised.
- If *Settings → Apple Pencil → Only Draw with Apple Pencil* is off, finger drawing works until the Pencil is first used on the page.

### Manual check on a real iPad (do this before go-live)

Automated tests emulate an iPad in Chromium (pen pointer events with pressure, 1180×820 at 2× and portrait). They do **not** run WebKit, so check these by hand in Safari on iPadOS 17 or later:

1. Writing follows the Pencil tip with no visible lag; line width changes with pressure.
2. Resting the palm does not draw, scroll or zoom; the page does not rubber-band while writing.
3. Eraser, Undo, Redo, Clear page, + Page and page switching.
4. *Saved* appears after writing; reload the page and the ink is still there.
5. Analyse handwriting with your chosen provider; review, correct, accept.
6. Rotate to portrait and back while on the writing tab.
7. From the Home Screen icon: sign in, write, and confirm nothing is cut off at the top or bottom.

## Limits

- Up to 20 pages per consultation; 20,000 strokes and 6 MB of ink per page; 4 MB PNG; 9 MB request.
- Recognition quality depends entirely on the provider. No provider was tested live for this PoC: the `http` and `google-vision` adapters were tested end to end against a local TLS test double that speaks each API.
- Extraction is English and dermatology-focused.
