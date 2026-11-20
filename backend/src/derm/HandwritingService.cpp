#include "HandwritingService.hpp"

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "derm/ClinicalTextExtractor.hpp"

namespace cf {

using json = nlohmann::json;

namespace {

struct Consult {
  std::string id, patientId, doctorId, status;
};

Consult loadConsult(Conn& c, const std::string& id) {
  if (!text::isUuid(id)) throw ApiError::notFound("Consultation");
  auto r = c.exec("SELECT id, patient_id, doctor_id, status FROM consultations WHERE id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Consultation");
  auto row = r.first();
  return {row.str("id"), row.str("patient_id"), row.str("doctor_id"), row.str("status")};
}

void requireTreatingDraft(const AuthContext& ctx, const Consult& cc) {
  if (!ctx.doctorId || *ctx.doctorId != cc.doctorId) throw ApiError::forbidden("Only the treating doctor can write in this consultation");
  if (cc.status != "DRAFT") throw ApiError::conflict("RECORD_LOCKED", "Finalized consultations cannot be changed. Add an addendum instead.");
}

void checkPage(int pageNo) {
  if (pageNo < 1 || pageNo > 20) throw ApiError::badRequest("Page number must be 1 to 20");
}

bool isPng(const std::string& b) {
  return b.size() >= 8 && static_cast<unsigned char>(b[0]) == 0x89 && b.compare(1, 3, "PNG") == 0;
}

// Sanity-check the vector ink so only well-formed data is stored.
int validateStrokes(const json& ink) {
  if (!ink.is_object() || !ink.contains("strokes") || !ink["strokes"].is_array())
    throw ApiError::badRequest("Ink must be an object with a 'strokes' list", {{"strokes", "Invalid ink data"}});
  if (ink["strokes"].size() > 20000) throw ApiError::badRequest("Too many strokes on one page; add a new page");
  for (auto& s : ink["strokes"]) {
    if (!s.is_object() || !s.contains("p") || !s["p"].is_array()) throw ApiError::badRequest("Malformed stroke", {{"strokes", "Invalid ink data"}});
    for (auto& pt : s["p"])
      if (!pt.is_array() || pt.size() < 2 || !pt[0].is_number() || !pt[1].is_number())
        throw ApiError::badRequest("Malformed stroke point", {{"strokes", "Invalid ink data"}});
  }
  return static_cast<int>(ink["strokes"].size());
}

}  // namespace

json HandwritingService::recognizerInfo() const {
  return {{"provider", m_recognizer->id()}, {"description", m_recognizer->description()}, {"enabled", m_recognizer->enabled()},
          {"sendsDataExternally", m_recognizer->sendsDataExternally()}};
}

json HandwritingService::transcriptionJson(const Row& r) const {
  auto s = [&](const char* col) { return r.isNull(col) ? json(nullptr) : json(r.str(col)); };
  auto j = [&](const char* col) { return r.isNull(col) ? json(nullptr) : json::parse(r.str(col)); };
  return {{"id", r.str("id")}, {"consultationId", r.str("consultation_id")}, {"provider", r.str("provider")},
          {"providerDetail", s("provider_detail")}, {"pageVersions", j("page_versions_t")}, {"recognizedText", s("recognized_text")},
          {"correctedText", s("corrected_text")}, {"extraction", j("extraction_t")}, {"acceptedFields", j("accepted_t")},
          {"status", r.str("status")}, {"createdBy", r.str("created_by_name")}, {"createdAt", *r.ts("created_at")},
          {"reviewedBy", s("reviewed_by_name")}, {"reviewedAt", r.isNull("reviewed_at") ? json(nullptr) : json(*r.ts("reviewed_at"))}};
}

static const char* TX_SELECT =
    "SELECT t.*, t.page_versions::text AS page_versions_t, t.extraction::text AS extraction_t, t.accepted_fields::text AS accepted_t, "
    "  cu.full_name AS created_by_name, ru.full_name AS reviewed_by_name "
    "FROM handwriting_transcriptions t JOIN users cu ON cu.id = t.created_by LEFT JOIN users ru ON ru.id = t.reviewed_by ";

json HandwritingService::overview(const AuthContext& ctx, const std::string& id) const {
  return m_db->withConn([&](Conn& c) {
    auto cc = loadConsult(c, id);
    json pages = json::array();
    c.exec("SELECT page_no, width, height, stroke_count, version, updated_at, image_enc IS NOT NULL AS has_image "
           "FROM handwritten_note_pages WHERE consultation_id = $1 ORDER BY page_no",
           Params(id))
        .each([&](const Row& r) {
          pages.push_back({{"pageNo", r.i64v("page_no")}, {"width", r.i64v("width")}, {"height", r.i64v("height")},
                           {"strokeCount", r.i64v("stroke_count")}, {"version", r.i64v("version")}, {"updatedAt", *r.ts("updated_at")},
                           {"hasImage", r.boolv("has_image")}});
        });
    json tx = json::array();
    c.exec(std::string(TX_SELECT) + "WHERE t.consultation_id = $1 ORDER BY t.created_at DESC", Params(id))
        .each([&](const Row& r) { tx.push_back(transcriptionJson(r)); });
    bool canEdit = cc.status == "DRAFT" && ctx.doctorId && *ctx.doctorId == cc.doctorId && ctx.has(perm::CLINICAL_WRITE);
    return json{{"consultationId", id}, {"pages", pages}, {"transcriptions", tx}, {"recognizer", recognizerInfo()}, {"canEdit", canEdit}};
  });
}

json HandwritingService::pageStrokes(const AuthContext& ctx, const std::string& id, int pageNo) const {
  checkPage(pageNo);
  return m_db->withConn([&](Conn& c) {
    loadConsult(c, id);
    auto r = c.exec("SELECT strokes_enc, version FROM handwritten_note_pages WHERE consultation_id = $1 AND page_no = $2", Params(id, pageNo));
    if (r.empty()) throw ApiError::notFound("Page");
    json ink = json::parse(m_cipher->decrypt(r.first().str("strokes_enc")));
    ink["version"] = r.first().i64v("version");
    return ink;
  });
}

std::string HandwritingService::pageImage(const AuthContext& ctx, const std::string& id, int pageNo) const {
  checkPage(pageNo);
  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    auto r = c.exec("SELECT image_enc FROM handwritten_note_pages WHERE consultation_id = $1 AND page_no = $2 AND image_enc IS NOT NULL",
                    Params(id, pageNo));
    if (r.empty()) throw ApiError::notFound("Page image");
    m_audit->record(c, ctx, AuditEvent{"HANDWRITING_VIEW", "consultation", id, cc.patientId, "SUCCESS", {{"page", pageNo}}});
    return m_cipher->decrypt(r.first().str("image_enc"));
  });
}

json HandwritingService::savePage(const AuthContext& ctx, const std::string& id, int pageNo, const json& in) {
  checkPage(pageNo);
  if (!in.is_object()) throw ApiError::badRequest("Request body must be a JSON object");
  Validator v;
  int width = in.value("width", 0), height = in.value("height", 0);
  v.check(width >= 100 && width <= 4000, "width", "100 to 4000");
  v.check(height >= 100 && height <= 6000, "height", "100 to 6000");
  v.check(in.contains("ink") && in["ink"].is_object(), "ink", "Ink data is required");
  v.throwIfAny("Handwriting page could not be saved");
  int strokes = validateStrokes(in["ink"]);
  std::string inkText = in["ink"].dump();
  if (inkText.size() > 6 * 1024 * 1024) throw ApiError::badRequest("Page ink is too large; continue on a new page");
  std::optional<std::string> png;
  if (strokes > 0) {
    if (!in.contains("imageBase64") || !in["imageBase64"].is_string())
      throw ApiError::badRequest("A rendered page image is required", {{"imageBase64", "Required"}});
    std::string b64 = in["imageBase64"].get<std::string>();
    auto comma = b64.find(',');
    if (b64.rfind("data:", 0) == 0 && comma != std::string::npos) b64 = b64.substr(comma + 1);
    std::string bytes;
    try {
      bytes = crypto::base64Decode(b64);
    } catch (...) {
      throw ApiError::badRequest("Page image is not valid base64", {{"imageBase64", "Invalid"}});
    }
    if (!isPng(bytes) || bytes.size() > 4 * 1024 * 1024) throw ApiError::badRequest("Page image must be a PNG up to 4 MB", {{"imageBase64", "Invalid image"}});
    png = bytes;
  }
  auto expected = in.contains("expectedVersion") && in["expectedVersion"].is_number() ? std::optional<int>(in["expectedVersion"].get<int>())
                                                                                       : std::nullopt;
  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    requireTreatingDraft(ctx, cc);
    auto cur = c.exec("SELECT version FROM handwritten_note_pages WHERE consultation_id = $1 AND page_no = $2 FOR UPDATE", Params(id, pageNo));
    if (!cur.empty() && expected && *expected != cur.first().i64v("version"))
      throw ApiError::conflict("VERSION_CONFLICT", "This page was changed in another window. Reload the page before writing.");
    std::optional<std::string> imgEnc, sha;
    if (png) {
      imgEnc = m_cipher->encrypt(*png);
      sha = crypto::sha256Hex(*png);
    }
    auto r = c.exec("INSERT INTO handwritten_note_pages (consultation_id, patient_id, page_no, width, height, stroke_count, strokes_enc, "
                    "  image_enc, image_sha256, created_by, updated_by) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$10) "
                    "ON CONFLICT (consultation_id, page_no) DO UPDATE SET width = EXCLUDED.width, height = EXCLUDED.height, "
                    "  stroke_count = EXCLUDED.stroke_count, strokes_enc = EXCLUDED.strokes_enc, image_enc = EXCLUDED.image_enc, "
                    "  image_sha256 = EXCLUDED.image_sha256, updated_by = EXCLUDED.updated_by, updated_at = now(), "
                    "  version = handwritten_note_pages.version + 1 "
                    "RETURNING version, updated_at",
                    Params(id, cc.patientId, pageNo, width, height, strokes, m_cipher->encrypt(inkText), imgEnc, sha, ctx.userId));
    auto row = r.first();
    // One audit entry per save: content is not logged, only its size and checksum.
    m_audit->record(c, ctx, AuditEvent{"HANDWRITING_SAVE", "consultation", id, cc.patientId, "SUCCESS",
                                       {{"page", pageNo}, {"strokes", strokes}, {"version", row.i64v("version")}, {"sha256", sha.value_or("")}}});
    return json{{"pageNo", pageNo}, {"version", row.i64v("version")}, {"strokeCount", strokes}, {"updatedAt", *row.ts("updated_at")}};
  });
}

json HandwritingService::deletePage(const AuthContext& ctx, const std::string& id, int pageNo) {
  checkPage(pageNo);
  return m_db->tx([&](Conn& c) {
    auto cc = loadConsult(c, id);
    requireTreatingDraft(ctx, cc);
    auto r = c.exec("DELETE FROM handwritten_note_pages WHERE consultation_id = $1 AND page_no = $2 RETURNING page_no", Params(id, pageNo));
    if (r.empty()) throw ApiError::notFound("Page");
    m_audit->record(c, ctx, AuditEvent{"HANDWRITING_PAGE_DELETE", "consultation", id, cc.patientId, "SUCCESS", {{"page", pageNo}}});
    return json{{"deleted", pageNo}};
  });
}

json HandwritingService::extractFor(Conn& c, const std::string& text) const {
  std::vector<derm::KnownMedicine> meds;
  c.exec("SELECT DISTINCT ON (generic_code) generic_code, generic_name FROM medication_catalogue "
         "WHERE generic_code IS NOT NULL AND is_active AND review_status <> 'RETIRED' ORDER BY generic_code, (brand_name IS NULL) DESC")
      .each([&](const Row& r) { meds.push_back({r.str("generic_code"), r.str("generic_name")}); });
  json ex = derm::extractClinicalText(text, meds);
  // Attach catalogue products to each mentioned medicine so the doctor can add it to the draft.
  // Medicines written by a name the catalogue lists only as a brand are matched by that brand.
  for (auto& m : ex["medicines"]) {
    json products = json::array();
    auto add = [&](const Row& r) {
      products.push_back({{"medicationId", r.str("id")}, {"genericName", r.str("generic_name")},
                          {"brandName", r.isNull("brand_name") ? json(nullptr) : json(r.str("brand_name"))},
                          {"strength", r.str("strength")}, {"dosageForm", r.str("dosage_form")}, {"route", r.str("default_route")}});
    };
    if (m["genericCode"].is_string()) {
      c.exec("SELECT id, generic_name, brand_name, strength, dosage_form, default_route FROM medication_catalogue "
             "WHERE generic_code = $1 AND is_active ORDER BY (brand_name IS NULL) DESC, strength LIMIT 6",
             Params(m["genericCode"].get<std::string>()))
          .each(add);
    } else {
      c.exec("SELECT id, generic_name, brand_name, strength, dosage_form, default_route, generic_code FROM medication_catalogue "
             "WHERE is_active AND (lower(brand_name) = lower($1) OR lower(generic_name) = lower($1)) ORDER BY strength LIMIT 6",
             Params(m["name"].get<std::string>()))
          .each([&](const Row& r) {
            add(r);
            if (!r.isNull("generic_code")) { m["genericCode"] = r.str("generic_code"); m["unmatched"] = false; }
          });
    }
    m["products"] = products;
  }
  return ex;
}

json HandwritingService::extract(const AuthContext& ctx, const std::string& id, const json& in) const {
  if (!in.is_object() || !in.contains("text") || !in["text"].is_string()) throw ApiError::badRequest("Text is required", {{"text", "Required"}});
  std::string t = in["text"].get<std::string>();
  if (t.size() > 20000) throw ApiError::badRequest("Text is limited to 20,000 characters");
  return m_db->withConn([&](Conn& c) {
    loadConsult(c, id);
    return extractFor(c, t);
  });
}

json HandwritingService::analyse(const AuthContext& ctx, const std::string& id, const json& in) {
  // Load page images first (short transaction), call the provider outside any transaction.
  struct Page {
    int no;
    long long version;
    std::string png;
  };
  std::vector<Page> pages;
  Consult cc;
  m_db->withConn([&](Conn& c) {
    cc = loadConsult(c, id);
    requireTreatingDraft(ctx, cc);
    c.exec("SELECT page_no, version, image_enc FROM handwritten_note_pages WHERE consultation_id = $1 AND image_enc IS NOT NULL "
           "AND stroke_count > 0 ORDER BY page_no",
           Params(id))
        .each([&](const Row& r) {
          pages.push_back({static_cast<int>(r.i64v("page_no")), r.i64v("version"), m_cipher->decrypt(r.str("image_enc"))});
        });
    return 0;
  });
  if (pages.empty()) throw ApiError::badRequest("Write on the pad before analysing: no saved handwriting was found");

  std::string recognized, detail;
  bool used = false, failed = false;
  if (m_recognizer->enabled()) {
    used = true;
    for (auto& p : pages) {
      auto r = m_recognizer->recognize(p.png);
      if (!r.ok) {
        failed = true;
        detail = r.error;
        break;
      }
      if (!recognized.empty() && !r.text.empty()) recognized += "\n";
      recognized += r.text;
    }
  }
  json pageVersions = json::array();
  for (auto& p : pages) pageVersions.push_back({{"page", p.no}, {"version", p.version}});

  return m_db->tx([&](Conn& c) {
    std::string provider = used ? m_recognizer->id() : "NONE";
    if (!used) detail = "No automatic recognition is configured; the doctor enters the text.";
    std::optional<std::string> rec;
    if (used && !failed) rec = recognized;
    json extraction = extractFor(c, rec.value_or(""));
    auto r = c.exec("INSERT INTO handwriting_transcriptions (consultation_id, patient_id, provider, provider_detail, page_versions, "
                    "  recognized_text, extraction, created_by) VALUES ($1,$2,$3,$4,$5::jsonb,$6,$7::jsonb,$8) RETURNING id",
                    Params(id, cc.patientId, provider, std::optional<std::string>(used ? m_recognizer->description() + (failed ? " - " + detail : "") : detail),
                           pageVersions.dump(), rec, extraction.dump(), ctx.userId));
    std::string tid = r.first().str("id");
    m_audit->record(c, ctx, AuditEvent{"HANDWRITING_RECOGNITION", "consultation", id, cc.patientId, failed ? "FAILURE" : "SUCCESS",
                                       {{"provider", provider}, {"pages", pages.size()}, {"external", used && m_recognizer->sendsDataExternally()},
                                        {"error", failed ? detail : ""}}});
    auto row = c.exec(std::string(TX_SELECT) + "WHERE t.id = $1", Params(tid)).first();
    json out = transcriptionJson(row);
    out["recognitionFailed"] = failed;
    if (failed) out["recognitionError"] = detail;
    out["recognizer"] = recognizerInfo();
    return out;
  });
}

json HandwritingService::accept(const AuthContext& ctx, const std::string& tid, const json& in) {
  if (!text::isUuid(tid)) throw ApiError::notFound("Transcription");
  if (!in.is_object() || !in.contains("correctedText") || !in["correctedText"].is_string())
    throw ApiError::badRequest("The reviewed text is required", {{"correctedText", "Required"}});
  std::string text = in["correctedText"].get<std::string>();
  if (text.find_first_not_of(" \t\r\n") == std::string::npos)
    throw ApiError::badRequest("The reviewed text is empty", {{"correctedText", "Enter or correct the text before accepting"}});
  if (text.size() > 20000) throw ApiError::badRequest("Text is limited to 20,000 characters");
  if (!in.contains("acceptedFields") || !in["acceptedFields"].is_object())
    throw ApiError::badRequest("Accepted fields are required", {{"acceptedFields", "Required"}});
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("SELECT consultation_id, status FROM handwriting_transcriptions WHERE id = $1 FOR UPDATE", Params(tid));
    if (r.empty()) throw ApiError::notFound("Transcription");
    if (r.first().str("status") != "PENDING_REVIEW") throw ApiError::conflict("ALREADY_REVIEWED", "This transcription has already been reviewed");
    auto cc = loadConsult(c, r.first().str("consultation_id"));
    requireTreatingDraft(ctx, cc);
    json extraction = extractFor(c, text);
    c.exec("UPDATE handwriting_transcriptions SET corrected_text = $2, extraction = $3::jsonb, accepted_fields = $4::jsonb, "
           "status = 'ACCEPTED', reviewed_by = $5, reviewed_at = now() WHERE id = $1",
           Params(tid, text, extraction.dump(), in["acceptedFields"].dump(), ctx.userId));
    // Supersede other pending analyses of the same consultation
    c.exec("UPDATE handwriting_transcriptions SET status = 'DISCARDED', reviewed_by = $2, reviewed_at = now() "
           "WHERE consultation_id = $1 AND status = 'PENDING_REVIEW' AND id <> $3",
           Params(cc.id, ctx.userId, tid));
    m_audit->record(c, ctx, AuditEvent{"HANDWRITING_TRANSCRIPTION_ACCEPT", "consultation", cc.id, cc.patientId, "SUCCESS",
                                       {{"transcriptionId", tid}, {"fields", in["acceptedFields"].size()}}});
    return transcriptionJson(c.exec(std::string(TX_SELECT) + "WHERE t.id = $1", Params(tid)).first());
  });
}

json HandwritingService::discard(const AuthContext& ctx, const std::string& tid, const json&) {
  if (!text::isUuid(tid)) throw ApiError::notFound("Transcription");
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("SELECT consultation_id, status FROM handwriting_transcriptions WHERE id = $1 FOR UPDATE", Params(tid));
    if (r.empty()) throw ApiError::notFound("Transcription");
    if (r.first().str("status") != "PENDING_REVIEW") throw ApiError::conflict("ALREADY_REVIEWED", "This transcription has already been reviewed");
    auto cc = loadConsult(c, r.first().str("consultation_id"));
    if (!ctx.doctorId || *ctx.doctorId != cc.doctorId) throw ApiError::forbidden("Only the treating doctor can review this transcription");
    c.exec("UPDATE handwriting_transcriptions SET status = 'DISCARDED', reviewed_by = $2, reviewed_at = now() WHERE id = $1", Params(tid, ctx.userId));
    m_audit->record(c, ctx, AuditEvent{"HANDWRITING_TRANSCRIPTION_DISCARD", "consultation", cc.id, cc.patientId, "SUCCESS", {{"transcriptionId", tid}}});
    return transcriptionJson(c.exec(std::string(TX_SELECT) + "WHERE t.id = $1", Params(tid)).first());
  });
}

}  // namespace cf
