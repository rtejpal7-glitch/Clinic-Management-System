#pragma once
// Dermatology assessment, photographs, decision-support panels, safety checks,
// outcomes, analytics and clinical-content review.
#include "BaseController.hpp"
#include "derm/DermService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

namespace perm {
inline const std::string PHOTO_READ = "photo.read";
inline const std::string PHOTO_WRITE = "photo.write";
inline const std::string CDS_USE = "cds.use";
inline const std::string CDS_REVIEW = "cds.review";
inline const std::string INSIGHTS_READ = "insights.read";
inline const std::string NETWORK_READ = "network.read";
}  // namespace perm

class DermController : public BaseController {
  std::shared_ptr<DermService> m_derm;
  using json = nlohmann::json;

  json body(const std::shared_ptr<IncomingRequest>& req) {
    auto b = bodyOf(req);
    if (!b || b->empty()) throw ApiError::badRequest("Request body is required");
    try {
      return json::parse(std::string(*b));
    } catch (...) {
      throw ApiError::badRequest("Malformed JSON request body");
    }
  }
  std::shared_ptr<OutgoingResponse> ok(const json& j, const Status& st = Status::CODE_200) { return rawJson(st, j.dump()); }
  AuthContext anyOf(const std::shared_ptr<IncomingRequest>& req, const std::string& a, const std::string& b) {
    auto ctx = auth(req, perm::AUTHENTICATED);
    if (ctx.mustChangePassword)
      throw ApiError(403, "PASSWORD_CHANGE_REQUIRED", "You must change your temporary password before continuing");
    if (!ctx.has(a)) m_auth->require(ctx, b, a + "|" + b);
    return ctx;
  }

 public:
  DermController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth, bool trustProxy,
                 std::shared_ptr<DermService> derm)
      : BaseController(om, std::move(auth), trustProxy), m_derm(std::move(derm)) {}

  ENDPOINT_INFO(vocab) { CF_DOC("Dermatology coded vocabulary (problem types, lesions, sites, ...)", "Dermatology") }
  ENDPOINT("GET", "/api/v1/derm/vocabulary", vocab, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { auth(request, perm::AUTHENTICATED); return ok(DermService::vocabulary()); });
  }

  ENDPOINT_INFO(getDerm) { CF_DOC("Dermatology assessment of a consultation", "Dermatology") }
  ENDPOINT("GET", "/api/v1/consultations/{id}/derm", getDerm, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->getAssessment(auth(request, perm::CLINICAL_READ), id)); });
  }

  ENDPOINT_INFO(putDerm) { CF_DOC("Save the dermatology assessment (draft consultations, treating doctor)", "Dermatology") }
  ENDPOINT("PUT", "/api/v1/consultations/{id}/derm", putDerm, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return ok(m_derm->saveAssessment(ctx, id, body(request)));
    });
  }

  ENDPOINT_INFO(listPhotos) { CF_DOC("Clinical photographs of a consultation (metadata)", "Dermatology") }
  ENDPOINT("GET", "/api/v1/consultations/{id}/photos", listPhotos, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->listPhotos(auth(request, perm::PHOTO_READ), id)); });
  }

  ENDPOINT_INFO(uploadPhoto) { CF_DOC("Add a clinical photograph (consent required; JPEG/PNG/WebP, max 6 MB, encrypted at rest)", "Dermatology") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/photos", uploadPhoto, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PHOTO_WRITE);
      return ok(m_derm->uploadPhoto(ctx, id, body(request)), Status::CODE_201);
    });
  }

  ENDPOINT_INFO(photoImage) { CF_DOC("Image bytes of a clinical photograph (every view is audited)", "Dermatology") }
  ENDPOINT("GET", "/api/v1/photos/{id}/image", photoImage, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto img = m_derm->photoImage(auth(request, perm::PHOTO_READ), id);
      return rawJson(Status::CODE_200, img.second, img.first.c_str());
    });
  }

  ENDPOINT_INFO(photoError) { CF_DOC("Mark a photograph entered-in-error (never deleted)", "Dermatology") }
  ENDPOINT("POST", "/api/v1/photos/{id}/entered-in-error", photoError, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PHOTO_WRITE);
      return ok(m_derm->markPhotoEnteredInError(ctx, id, body(request)));
    });
  }

  ENDPOINT_INFO(panelA) { CF_DOC("Panel A - clinical knowledge: possible conditions, questions, red flags, guideline options", "Decision support") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/cds/clinical", panelA, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->clinicalPanel(auth(request, perm::CDS_USE), id)); });
  }

  ENDPOINT_INFO(panelB) { CF_DOC("Panel B - the doctor's own prescribing in comparable previous cases", "Decision support") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/cds/my-patterns", panelB, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->myPatternsPanel(auth(request, perm::CDS_USE), id)); });
  }

  ENDPOINT_INFO(panelC) { CF_DOC("Panel C - de-identified prescribing patterns across participating ClinicFlow clinics", "Decision support") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/cds/network", panelC, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::NETWORK_READ);
      m_auth->require(ctx, perm::CDS_USE, "cds");
      return ok(m_derm->networkPanel(ctx, id));
    });
  }

  ENDPOINT_INFO(safety) { CF_DOC("Safety check of the saved draft prescription (allergy, pregnancy, age, duplicates, interactions)", "Decision support") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/safety-check", safety, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->safetyCheck(auth(request, perm::CLINICAL_READ), id)); });
  }

  ENDPOINT_INFO(decisions) { CF_DOC("Suggestions shown vs final prescription, safety overrides and outcomes", "Decision support") }
  ENDPOINT("GET", "/api/v1/consultations/{id}/decisions", decisions, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->decisions(auth(request, perm::CLINICAL_READ), id)); });
  }

  ENDPOINT_INFO(outcome) { CF_DOC("Record a follow-up treatment outcome (RESOLVED / IMPROVED / UNCHANGED / WORSENED)", "Decision support") }
  ENDPOINT("POST", "/api/v1/consultations/{id}/outcome", outcome, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CLINICAL_WRITE);
      return ok(m_derm->recordOutcome(ctx, id, body(request)), Status::CODE_201);
    });
  }

  ENDPOINT_INFO(doctorAnalytics) { CF_DOC("The signed-in doctor's dashboard and own prescribing analytics", "Analytics") }
  ENDPOINT("GET", "/api/v1/analytics/doctor", doctorAnalytics, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->doctorAnalytics(auth(request, perm::INSIGHTS_READ))); });
  }

  ENDPOINT_INFO(networkAnalytics) { CF_DOC("De-identified network coverage by diagnosis (thresholds applied)", "Analytics") }
  ENDPOINT("GET", "/api/v1/analytics/network", networkAnalytics, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return ok(m_derm->networkAnalytics(auth(request, perm::NETWORK_READ))); });
  }

  ENDPOINT_INFO(content) { CF_DOC("Clinical decision-support content and medicine safety data with review status", "Clinical content") }
  ENDPOINT("GET", "/api/v1/cds/content", content, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = anyOf(request, perm::CDS_REVIEW, perm::CATALOGUE_MANAGE);
      return ok(m_derm->contentList(ctx, q(request, "status").value_or("ALL")));
    });
  }

  ENDPOINT_INFO(review) { CF_DOC("Approve, retire or reopen a content item (clinical reviewers only)", "Clinical content") }
  ENDPOINT("POST", "/api/v1/cds/content/{type}/{id}/review", review, PATH(String, type), PATH(String, id),
           REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CDS_REVIEW);
      return ok(m_derm->reviewContent(ctx, type, id, body(request)));
    });
  }

  ENDPOINT_INFO(medClinical) { CF_DOC("Edit a medicine's clinical information (returns it to UNREVIEWED)", "Clinical content") }
  ENDPOINT("PUT", "/api/v1/catalogue/medications/{id}/clinical", medClinical, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = anyOf(request, perm::CATALOGUE_MANAGE, perm::CDS_REVIEW);
      return ok(m_derm->updateMedicationClinical(ctx, id, body(request)));
    });
  }

  ENDPOINT_INFO(optOut) { CF_DOC("Record the patient's choice about network use of de-identified data", "Patients") }
  ENDPOINT("PUT", "/api/v1/patients/{id}/network-opt-out", optOut, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::PATIENT_UPDATE);
      return ok(m_derm->setPatientNetworkOptOut(ctx, id, body(request)));
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
