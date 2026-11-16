#pragma once
// Handwritten consultation notes (stylus ink, recognition, doctor review).
#include "BaseController.hpp"
#include "derm/HandwritingService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class HandwritingController : public BaseController {
  std::shared_ptr<HandwritingService> m_hw;
  using json = nlohmann::json;

  json body(const std::shared_ptr<IncomingRequest>& req) {
    auto b = bodyOf(req);
    if (!b || b->empty()) return json::object();
    try {
      return json::parse(std::string(*b));
    } catch (...) {
      throw ApiError::badRequest("Malformed JSON request body");
    }
  }
  static int pageNo(const String& s) {
    try {
      return std::stoi(std::string(*s));
    } catch (...) {
      throw ApiError::badRequest("Invalid page number");
    }
  }
  std::shared_ptr<OutgoingResponse> ok(const json& j, const Status& st = Status::CODE_200) { return rawJson(st, j.dump()); }

 public:
  HandwritingController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth, bool trustProxy,
                        std::shared_ptr<HandwritingService> hw)
      : BaseController(om, std::move(auth), trustProxy), m_hw(std::move(hw)) {}

  ENDPOINT_INFO(hwOverview) { CF_DOC("Handwritten note pages and transcriptions of a consultation", "Handwritten notes") }
  ENDPOINT("GET", "/api/v1/consultations/{id}/handwriting", hwOverview, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_hw->overview(auth(request, perm::CLINICAL_READ), id)); });
  }

  ENDPOINT_INFO(hwStrokes) { CF_DOC("Vector ink of one page (decrypted)", "Handwritten notes") }
  ENDPOINT("GET", "/api/v1/consultations/{id}/handwriting/pages/{page}/ink", hwStrokes, PATH(String, id), PATH(String, page),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_hw->pageStrokes(auth(request, perm::CLINICAL_READ), id, pageNo(page))); });
  }

  ENDPOINT_INFO(hwImage) { CF_DOC("Rendered PNG of one page (view is audited)", "Handwritten notes") }
  ENDPOINT("GET", "/api/v1/consultations/{id}/handwriting/pages/{page}/image", hwImage, PATH(String, id), PATH(String, page),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return rawJson(Status::CODE_200, m_hw->pageImage(auth(request, perm::CLINICAL_READ), id, pageNo(page)), "image/png"); });
  }

  ENDPOINT_INFO(hwSave) { CF_DOC("Save one page: vector ink + rendered PNG (treating doctor, draft only)", "Handwritten notes") }
  ENDPOINT("PUT", "/api/v1/consultations/{id}/handwriting/pages/{page}", hwSave, PATH(String, id), PATH(String, page),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return ok(m_hw->savePage(ctx, id, pageNo(page), body(request)));
    });
  }

  ENDPOINT_INFO(hwDelete) { CF_DOC("Remove a page (draft only)", "Handwritten notes") }
  ENDPOINT("DELETE", "/api/v1/consultations/{id}/handwriting/pages/{page}", hwDelete, PATH(String, id), PATH(String, page),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_hw->deletePage(auth(request, perm::CLINICAL_WRITE), id, pageNo(page))); });
  }

  ENDPOINT_INFO(hwAnalyse) { CF_DOC("Analyse handwriting: recognition (if configured) + proposed structured fields; creates a transcription awaiting review", "Handwritten notes") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/handwriting/analyse", hwAnalyse, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return ok(m_hw->analyse(ctx, id, body(request)), Status::CODE_201);
    });
  }

  ENDPOINT_INFO(hwExtract) { CF_DOC("Propose structured fields from (corrected) note text; stores nothing", "Handwritten notes") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/handwriting/extract", hwExtract, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_READ);
      return ok(m_hw->extract(ctx, id, body(request)));
    });
  }

  ENDPOINT_INFO(hwAccept) { CF_DOC("Doctor accepts the reviewed text and the fields applied to the consultation", "Handwritten notes") }
  ENDPOINT("POST", "/api/v1/handwriting/transcriptions/{tid}/accept", hwAccept, PATH(String, tid), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return ok(m_hw->accept(ctx, tid, body(request)));
    });
  }

  ENDPOINT_INFO(hwDiscard) { CF_DOC("Discard a transcription without applying it", "Handwritten notes") }
  ENDPOINT("POST", "/api/v1/handwriting/transcriptions/{tid}/discard", hwDiscard, PATH(String, tid), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return ok(m_hw->discard(ctx, tid, body(request)));
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
