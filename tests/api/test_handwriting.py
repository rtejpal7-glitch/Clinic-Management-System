"""Handwritten consultation notes: ink storage, recognition providers, structured
extraction, doctor review/acceptance, locking and access control.

The recognition-provider tests start a second API server configured with
HWR_PROVIDER=http against a local HTTPS test double (self-signed certificate),
exercising the real TLS client and the provider contract. They run when
CF_SERVER_BIN points to the built server binary and CF_SERVER_ENV_FILE to its
environment file (and are skipped otherwise)."""
import base64
import json
import os
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, HTTPServer

import pytest
import requests

from conftest import PASSWORD, derm_visit, make_doctor


class AltClient:
    """Client for the second API server started by the provider fixture."""

    def __init__(self, base, token):
        self.base, self.h = base, {"Authorization": f"Bearer {token}"}

    def get(self, path, **kw):
        return requests.get(self.base + path, headers=self.h, timeout=60, **kw)

    def put(self, path, json=None):
        return requests.put(self.base + path, headers=self.h, json=json, timeout=60)

    def post(self, path, json=None):
        return requests.post(self.base + path, headers=self.h, json=json if json is not None else {}, timeout=60)



def png(w=200, h=120):
    def chunk(t, d):
        c = struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        return struct.pack(">I", len(d)) + t + d + c
    raw = b"".join(b"\x00" + b"\xff\xff\xff" * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


INK = {"v": 1, "strokes": [{"t": "pen", "c": "#1d2a44", "s": 2.2, "p": [[10, 10, 0.5, 0], [20, 14, 0.6, 16], [32, 20, 0.6, 32]]},
                           {"t": "pen", "c": "#1d2a44", "s": 2.2, "p": [[40, 40, 0.5, 0], [60, 44, 0.5, 16]]}]}
NOTE = ("C/o itchy raised wheals on arms and back x 5/7\nNo fever. No lip swelling\n"
        "O/E erythematous wheals, generalised\nImp: ?urticaria\nRx fexofenadine 120mg OD x 2 weeks")


def page_body(ink=INK, image=True, **extra):
    b = {"width": 1000, "height": 1400, "ink": ink}
    if image:
        b["imageBase64"] = "data:image/png;base64," + base64.b64encode(png()).decode()
    b.update(extra)
    return b


@pytest.fixture(scope="module")
def hwdoc(admin):
    return make_doctor(admin, "Dr. Ink")


def test_save_page_overview_ink_and_image(receptionist, hwdoc, admin):
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    r = hwdoc.put(f"/consultations/{c['id']}/handwriting/pages/1", page_body())
    assert r.status_code == 200, r.text
    assert r.json()["version"] == 1 and r.json()["strokeCount"] == 2
    r = hwdoc.put(f"/consultations/{c['id']}/handwriting/pages/1", page_body(expectedVersion=1))
    assert r.json()["version"] == 2
    # stale version from another window is refused
    assert hwdoc.put(f"/consultations/{c['id']}/handwriting/pages/1", page_body(expectedVersion=1)).status_code == 409
    ov = hwdoc.get(f"/consultations/{c['id']}/handwriting").json()
    assert ov["canEdit"] and ov["pages"][0]["pageNo"] == 1 and ov["pages"][0]["hasImage"]
    assert ov["recognizer"]["provider"] in ("NONE", "HTTP", "GOOGLE_VISION")
    ink = hwdoc.get(f"/consultations/{c['id']}/handwriting/pages/1/ink").json()
    assert ink["strokes"] == INK["strokes"] and ink["version"] == 2
    img = hwdoc.get(f"/consultations/{c['id']}/handwriting/pages/1/image")
    assert img.status_code == 200 and img.content.startswith(b"\x89PNG") and img.headers["Cache-Control"] == "no-store"
    logs = {l["action"] for l in admin.get("/audit-logs", params={"entityId": c["id"]}).json()["items"]}
    assert {"HANDWRITING_SAVE", "HANDWRITING_VIEW"} <= logs


def test_page_validation(receptionist, hwdoc):
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    base = f"/consultations/{c['id']}/handwriting/pages"
    assert hwdoc.put(f"{base}/0", page_body()).status_code == 400
    assert hwdoc.put(f"{base}/1", page_body(ink={"strokes": [{"p": [["x", 1]]}]})).status_code == 400
    bad_img = page_body()
    bad_img["imageBase64"] = base64.b64encode(b"GIF89a....").decode()
    assert hwdoc.put(f"{base}/1", bad_img).status_code == 400
    assert hwdoc.put(f"{base}/1", page_body(image=False)).status_code == 400  # ink needs a rendering
    empty = hwdoc.put(f"{base}/2", page_body(ink={"v": 1, "strokes": []}, image=False))
    assert empty.status_code == 200 and empty.json()["strokeCount"] == 0  # a cleared page
    assert hwdoc.delete(f"{base}/2").status_code == 200


def test_access_control(receptionist, hwdoc, other_doctor):
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    path = f"/consultations/{c['id']}/handwriting"
    hwdoc.put(f"{path}/pages/1", page_body())
    assert other_doctor.put(f"{path}/pages/1", page_body()).status_code == 403
    assert other_doctor.post(f"{path}/analyse").status_code == 403
    assert receptionist.get(path).status_code == 403
    assert receptionist.get(f"{path}/pages/1/image").status_code == 403


def test_prescription_lines_from_the_ipad_note(receptionist, hwdoc):
    """Both lines of a written prescription are proposed; treatment details are not read as findings."""
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    note = ". Benzoyl peroxide, two times a day for 15 days.\n\nApply betadine solution on face for 10 days every evening"
    ex = hwdoc.post(f"/consultations/{c['id']}/handwriting/extract", {"text": note}).json()
    meds = ex["medicines"]
    assert [m["name"] for m in meds] == ["Benzoyl Peroxide", "Betadine"]
    bp, bet = meds
    assert bp["genericCode"] == "BENZOYL_PEROXIDE" and bp["products"] and bp["frequency"] == "Twice daily" and bp["durationDays"] == 15
    assert bet["unmatched"] is True and bet["products"] == [] and bet["route"] == "TOPICAL" and bet["dosageForm"] == "SOLUTION"
    assert bet["frequency"] == "Once daily, evening" and bet["durationDays"] == 10
    assert ex["assessment"]["duration"] is None and ex["assessment"]["bodySites"] == []
    assert "complaint" not in ex["sections"] and ex["sections"]["plan"].count("\n") == 1


def test_analyse_without_provider_and_extract(receptionist, hwdoc):
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    path = f"/consultations/{c['id']}/handwriting"
    assert hwdoc.post(f"{path}/analyse").status_code == 400  # nothing written yet
    hwdoc.put(f"{path}/pages/1", page_body())
    t = hwdoc.post(f"{path}/analyse")
    assert t.status_code == 201, t.text
    tx = t.json()
    if tx["provider"] == "NONE":
        assert tx["recognizedText"] is None and tx["status"] == "PENDING_REVIEW"
        assert "No automatic recognition" in tx["providerDetail"]
    ex = hwdoc.post(f"{path}/extract", {"text": NOTE}).json()
    a = ex["assessment"]
    assert {"ITCHING"} <= {s["code"] for s in a["symptoms"]}
    assert "FEVER" in {n["code"] for n in ex["negatives"]}
    assert a["duration"]["value"] == 5 and a["duration"]["unit"] == "DAYS"
    assert ex["diagnoses"][0]["code"] == "L50.9"
    med = ex["medicines"][0]
    assert med["genericCode"] == "FEXOFENADINE" and med["products"] and med["frequency"] == "Once daily"
    # extraction alone never changes the record
    cons = hwdoc.get(f"/consultations/{c['id']}").json()
    assert [d["code"] for d in cons["diagnoses"]] == ["L50.9"]  # only the one derm_visit saved, nothing added
    assert cons["prescription"]["items"] == []


def test_accept_requires_review_and_is_immutable(receptionist, hwdoc, admin):
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    path = f"/consultations/{c['id']}/handwriting"
    hwdoc.put(f"{path}/pages/1", page_body())
    tx = hwdoc.post(f"{path}/analyse").json()
    assert hwdoc.post(f"/handwriting/transcriptions/{tx['id']}/accept", {"correctedText": "  ", "acceptedFields": {}}).status_code == 400
    r = hwdoc.post(f"/handwriting/transcriptions/{tx['id']}/accept",
                   {"correctedText": NOTE, "acceptedFields": {"chiefComplaint": "itchy wheals", "symptoms": ["ITCHING"]}})
    assert r.status_code == 200, r.text
    acc = r.json()
    assert acc["status"] == "ACCEPTED" and acc["reviewedBy"] and acc["correctedText"] == NOTE
    assert acc["extraction"]["diagnoses"][0]["code"] == "L50.9"
    assert hwdoc.post(f"/handwriting/transcriptions/{tx['id']}/accept", {"correctedText": "x", "acceptedFields": {}}).status_code == 409
    # second pending analysis is superseded when one is accepted
    tx2 = hwdoc.post(f"{path}/analyse").json()
    hwdoc.post(f"/handwriting/transcriptions/{tx2['id']}/discard")
    ov = hwdoc.get(path).json()
    assert [t["status"] for t in ov["transcriptions"]].count("ACCEPTED") == 1
    logs = {l["action"] for l in admin.get("/audit-logs", params={"entityId": c["id"]}).json()["items"]}
    assert {"HANDWRITING_RECOGNITION", "HANDWRITING_TRANSCRIPTION_ACCEPT", "HANDWRITING_TRANSCRIPTION_DISCARD"} <= logs


def test_pages_lock_with_consultation_and_appear_in_history(receptionist, hwdoc):
    c, p = derm_visit(receptionist, hwdoc, finalize=False)
    path = f"/consultations/{c['id']}/handwriting"
    hwdoc.put(f"{path}/pages/1", page_body())
    tx = hwdoc.post(f"{path}/analyse").json()
    hwdoc.post(f"/handwriting/transcriptions/{tx['id']}/accept", {"correctedText": NOTE, "acceptedFields": {"x": 1}})
    assert hwdoc.post(f"/consultations/{c['id']}/finalize").status_code == 200
    assert hwdoc.put(f"{path}/pages/1", page_body()).status_code == 409
    assert hwdoc.delete(f"{path}/pages/1").status_code == 409
    assert hwdoc.post(f"{path}/analyse").status_code == 409
    assert hwdoc.get(f"{path}/pages/1/image").status_code == 200  # still readable
    tl = hwdoc.get(f"/patients/{p['id']}/timeline").json()
    entry = next(e for e in tl if e["consultationId"] == c["id"])
    assert entry["handwrittenPages"] == 1 and entry["handwritingTranscribed"] is True


def test_handwriting_isolated_between_clinics(receptionist, hwdoc, clinic_b):
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    hwdoc.put(f"/consultations/{c['id']}/handwriting/pages/1", page_body())
    doc_b = make_doctor(clinic_b, "Dr. Other Clinic")
    assert doc_b.get(f"/consultations/{c['id']}/handwriting").status_code == 404
    assert doc_b.get(f"/consultations/{c['id']}/handwriting/pages/1/image").status_code == 404


# ---------------------------------------------------------------- recognition provider (TLS test double)
class _Recognizer(BaseHTTPRequestHandler):
    calls = []

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        text = "c/o itcy wheals on arms x 3/7\nno fever\n?urticarla"
        if self.path == "/v1/images:annotate":  # Google Cloud Vision request/response shape
            req = body["requests"][0]
            _Recognizer.calls.append({"key": self.headers.get("X-Goog-Api-Key"), "feature": req["features"][0]["type"],
                                      "hint": req["imageContext"]["languageHints"][0], "png": base64.b64decode(req["image"]["content"])[:4]})
            out = json.dumps({"responses": [{"fullTextAnnotation": {"text": text}}]}).encode()
        else:  # generic self-hosted contract
            _Recognizer.calls.append({"auth": self.headers.get("Authorization"), "mime": body.get("mimeType"),
                                      "png": base64.b64decode(body["imageBase64"])[:4]})
            out = json.dumps({"text": text}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    def log_message(self, *a):
        pass


def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


@pytest.fixture(scope="module", params=["http", "google-vision"])
def hwr_server(request):
    binary = os.environ.get("CF_SERVER_BIN")
    if not binary or not shutil.which("openssl"):
        pytest.skip("CF_SERVER_BIN not set; provider integration test skipped")
    tmp = tempfile.mkdtemp()
    cert, key = os.path.join(tmp, "cert.pem"), os.path.join(tmp, "key.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert, "-days", "1",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"], check=True, capture_output=True)
    rport = _free_port()
    httpd = HTTPServer(("127.0.0.1", rport), _Recognizer)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    aport = _free_port()
    env = dict(os.environ)
    # Server configuration (DATABASE_URL, keys ...) from the same env file the main test server uses
    env_file = os.environ.get("CF_SERVER_ENV_FILE")
    if env_file:
        for line in open(env_file):
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                env.setdefault(k.removeprefix("export ").strip(), v.strip().strip('"'))
    env.update(PORT=str(aport), HWR_PROVIDER=request.param, HWR_HTTP_URL=f"https://localhost:{rport}/recognize",
               HWR_HTTP_TOKEN="test-token", HWR_HTTP_LABEL="Local test recogniser", HWR_CA_FILE=cert,
               HWR_GOOGLE_API_KEY="test-key", HWR_GOOGLE_ENDPOINT=f"https://localhost:{rport}")
    log = open(os.path.join(tmp, "server.log"), "w")
    proc = subprocess.Popen([binary], env=env, stdout=log, stderr=log, cwd=os.path.dirname(os.path.dirname(binary)))
    base = f"http://localhost:{aport}/api/v1"
    for _ in range(60):
        try:
            if requests.get(base + "/health", timeout=1).ok:
                break
        except Exception:
            time.sleep(0.25)
    else:
        proc.terminate()
        pytest.fail("second API server did not start: " + open(os.path.join(tmp, "server.log")).read()[-500:])
    yield base, request.param
    proc.terminate()
    httpd.shutdown()


def test_recognition_through_configured_provider(hwr_server, receptionist, hwdoc):
    hwr_server, provider = hwr_server
    c, _ = derm_visit(receptionist, hwdoc, finalize=False)
    tok = requests.post(hwr_server + "/auth/login", json={"username": hwdoc.username, "password": PASSWORD}).json()["token"]
    doc = AltClient(hwr_server, tok)
    path = f"/consultations/{c['id']}/handwriting"
    assert doc.put(f"{path}/pages/1", page_body()).status_code == 200
    ov = doc.get(path).json()
    expected = "HTTP" if provider == "http" else "GOOGLE_VISION"
    assert ov["recognizer"]["provider"] == expected and ov["recognizer"]["enabled"] and ov["recognizer"]["sendsDataExternally"]
    tx = doc.post(f"{path}/analyse").json()
    assert tx["provider"] == expected and not tx["recognitionFailed"], tx
    assert tx["recognizedText"].startswith("c/o itcy wheals")
    ex = tx["extraction"]
    assert "ITCHING" in {s["code"] for s in ex["assessment"]["symptoms"]}       # "itcy" matched approximately
    assert any(x["original"] == "itcy" for x in ex["corrections"])
    assert ex["diagnoses"][0]["code"] == "L50.9"                                   # "urticarla"
    assert ex["assessment"]["duration"]["value"] == 3
    call = _Recognizer.calls[-1]
    if provider == "http":
        assert call["auth"] == "Bearer test-token" and call["mime"] == "image/png" and call["png"] == b"\x89PNG"
    else:
        assert call == {"key": "test-key", "feature": "DOCUMENT_TEXT_DETECTION", "hint": "en-t-i0-handwrit", "png": b"\x89PNG"}
