# Handwritten consultation notes (v0.3)

Doctors can write the consultation by hand on an iPad with Apple Pencil, as on a paper case sheet. The tab has two sections on one screen: **Write** (the page) and **Text** (the note as text, with a short list of what will go into the consultation). One button adds it.

**The doctor decides.** Nothing reaches the consultation until the doctor presses *Add to consultation*, and any item can be left out first. Diagnoses are added as *provisional*; medicines go into the *draft* prescription with the line as written and "check dose". The consultation is only finalized when the doctor presses Finalize, as before.

## Workflow

1. **Start consultation.** On a tablet the **Handwritten notes** tab opens by itself. On a desktop it is the tab after *Notes*.
2. **Write.** Pen, three colours, eraser, undo/redo, clear page, up to 20 pages. Line width follows Pencil pressure. Pages save themselves a moment after the pen lifts (*Saved*).
3. **Text.**
   - *With a recognition service set up* (below), the page is converted automatically a moment after the doctor stops writing, and the text appears in the Text section.
   - *Without one* (the default), the doctor writes in the Text box with Apple Pencil, and iPadOS **Scribble** turns it into typed text, or types.
   - Either way the doctor can correct the text. Words that look like misread medical terms ("erythematus", "itcy") are offered as one-tap fixes.
4. **Will be added.** Under the text, ClinicFlow lists what it found, one line each:
   - **Rx**: every prescription line, with how often and how long. A medicine that is not in the catalogue is still listed and added as written (free-text item), so no line is dropped. If it has no catalogue entry and no route (for example "betadine on face" without "apply"), it stays in the treatment plan and the doctor adds it on the Prescription tab.
   - **Dx**: diagnoses written, as ICD-10, provisional.
   - **Findings**: symptoms, duration, affected areas, lesions and so on, as chips the doctor can switch off.
   - **Notes**: complaint, examination, treatment plan.
   
   Everything recognised exactly is ticked; approximate matches are not. Prescription lines are never read as findings: "apply on face for 10 days" is where and how long to treat, not the affected area or how long the problem has lasted. Negatives ("no fever") are listed as absent.
5. **Add to consultation.** The ticked items go into the notes, dermatology assessment, diagnoses and draft prescription. The text is stored as an approved transcription with the doctor's name and time. If the doctor writes more and adds again, only new lines and new medicines are added; nothing is repeated.
6. **Decision support.** The *Decision support* button opens the dermatology panels (A/B/C) on the approved information.
7. **Record.** After finalization the pages are read-only (database trigger). The tab shows the handwriting beside the approved text, and the patient's history shows "✍ N handwritten pages · doctor-approved transcription".

## What is stored

| Table | Contents | Protection |
|---|---|---|
| `handwritten_note_pages` | Vector ink (points with pressure and time) and a PNG rendering per page, SHA-256 of the image, version | AES-256-GCM at rest; RLS by clinic; locked when the consultation is finalized; delete only while draft |
| `handwriting_transcriptions` | Provider, recognised text (machine output, immutable), the doctor's final text, extraction result, accepted fields, status, reviewer and time. One is created each time the page is converted or the doctor adds to the consultation | RLS by clinic; no deletes; immutable once ACCEPTED or DISCARDED; superseded pending drafts are marked DISCARDED |

Audit events: `HANDWRITING_SAVE` (with image hash), `HANDWRITING_VIEW`, `HANDWRITING_DELETE`, `HANDWRITING_RECOGNITION` (provider, whether data left ClinicFlow, error), `HANDWRITING_ACCEPT`, `HANDWRITING_DISCARD`. Only the treating doctor can write or accept, and only while the consultation is a draft.

## Recognition providers

Recognition is a pluggable port, like the ABDM gateway. Set it on the **backend** (`.env` for Docker):

| `HWR_PROVIDER` | What happens | Settings |
|---|---|---|
| `none` (default) | No automatic conversion. The doctor writes the text with Scribble or types it. Nothing leaves ClinicFlow. | — |
| `google-vision` | Page PNGs are sent to Google Cloud Vision `DOCUMENT_TEXT_DETECTION` with the handwriting language hint. | `HWR_GOOGLE_API_KEY`, optional `HWR_GOOGLE_ENDPOINT` (e.g. a regional endpoint) |
| `http` | Page PNGs are sent to your own service (for example a self-hosted handwriting OCR model inside your network). | `HWR_HTTP_URL`, optional `HWR_HTTP_TOKEN` (sent as `Authorization: Bearer`), `HWR_HTTP_LABEL` (shown to doctors) |

**On Render:** open *clinicflow-api* → *Environment*, set `HWR_PROVIDER` to `google-vision` and add `HWR_GOOGLE_API_KEY`, then save (Render redeploys). The API key comes from a Google Cloud project with the Cloud Vision API enabled and billing set up; Google bills per page converted after a monthly free allowance (check current Google pricing).

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
- The Text section names the service and says that page images are sent to it.

## Clinical text extraction

Extraction (`backend/src/derm/ClinicalTextExtractor.cpp`, engine `note-extract-1.1`) is **deterministic and rule-based**, not AI. It understands common case-sheet shorthand: section headings (c/o, h/o, o/e, imp, ddx, rx, plan), durations (x 3/7, 2/52, 6/12, "two weeks"), negation (no, nil, denies, -ve), query diagnoses (?, r/o, likely), and prescription shorthand and words (OD, BD, TDS, QID, HS, SOS, 1-0-1, "2 times a day", "every evening"). Lines that read as prescription lines (a known medicine, or words like apply, tab, cream, lotion, solution, or a frequency) go to the treatment plan; medicines not in the catalogue are matched by brand name if the catalogue has one, otherwise listed as written. It is tuned for dermatology vocabulary; terms outside its lexicon are left in the text and not turned into fields. It never invents a finding that is not in the text (unit test `extract_does_not_invent_findings`).

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
5. With your chosen provider, check the text appears a moment after you stop writing; correct it and add to the consultation.
6. Rotate to portrait and back while on the writing tab.
7. From the Home Screen icon: sign in, write, and confirm nothing is cut off at the top or bottom.

## Limits

- Up to 20 pages per consultation; 20,000 strokes and 6 MB of ink per page; 4 MB PNG; 9 MB request.
- Recognition quality depends entirely on the provider. No provider was tested live for this PoC: the `http` and `google-vision` adapters were tested end to end against a local TLS test double that speaks each API.
- Extraction is English and dermatology-focused.
