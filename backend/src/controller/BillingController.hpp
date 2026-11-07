#pragma once
#include "BaseController.hpp"
#include "dto/BillingDtos.hpp"
#include "service/BillingService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace cf {

class BillingController : public BaseController {
  std::shared_ptr<BillingService> m_svc;

 public:
  BillingController(const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& om, std::shared_ptr<AuthService> auth, bool tp,
                    std::shared_ptr<BillingService> svc)
      : BaseController(om, std::move(auth), tp), m_svc(std::move(svc)) {}

  ENDPOINT_INFO(services) {
    CF_DOC("Billable services catalogue (includeInactive=true for all)", "Billing")
    info->addResponse<oatpp::Vector<oatpp::Object<dto::ServiceDto>>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/billing/services", services, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::AUTHENTICATED);
      if (!ctx.has(perm::BILLING_READ)) m_auth->require(ctx, perm::CATALOGUE_MANAGE, "billing.services");
      return createDtoResponse(Status::CODE_200, m_svc->listServices(q(request, "includeInactive").value_or("") == "true"));
    });
  }

  ENDPOINT_INFO(createService) {
    CF_DOC("Add a billable service", "Billing")
    info->addConsumes<Object<dto::ServiceDto>>("application/json");
    info->addResponse<Object<dto::ServiceDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/billing/services", createService, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CATALOGUE_MANAGE);
      return createDtoResponse(Status::CODE_201, m_svc->saveService(ctx, "", parse<Object<dto::ServiceDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(updateService) {
    CF_DOC("Edit or deactivate a billable service", "Billing")
    info->addConsumes<Object<dto::ServiceDto>>("application/json");
    info->addResponse<Object<dto::ServiceDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("PUT", "/api/v1/billing/services/{id}", updateService, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::CATALOGUE_MANAGE);
      return createDtoResponse(Status::CODE_200, m_svc->saveService(ctx, id, parse<Object<dto::ServiceDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(list) {
    CF_DOC("List bills (date, status, patientId, q, page, size)", "Billing")
    info->addResponse<Object<dto::BillPageDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/bills", list, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::BILLING_READ);
      BillFilter f;
      f.date = q(request, "date");
      f.status = q(request, "status");
      f.patientId = q(request, "patientId");
      f.q = q(request, "q");
      f.page = qInt(request, "page", 1);
      f.size = qInt(request, "size", 50);
      return createDtoResponse(Status::CODE_200, m_svc->list(f));
    });
  }

  ENDPOINT_INFO(create) {
    CF_DOC("Create a bill / invoice", "Billing")
    info->addConsumes<Object<dto::BillCreateDto>>("application/json");
    info->addResponse<Object<dto::BillDto>>(Status::CODE_201, "application/json");
  }
  ENDPOINT("POST", "/api/v1/bills", create, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::BILLING_MANAGE);
      return createDtoResponse(Status::CODE_201, m_svc->create(ctx, parse<Object<dto::BillCreateDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(get) {
    CF_DOC("Get a bill with items and payments", "Billing")
    info->addResponse<Object<dto::BillDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/bills/{id}", get, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] { return createDtoResponse(Status::CODE_200, m_svc->get(auth(request, perm::BILLING_READ), id)); });
  }

  ENDPOINT_INFO(pay) {
    CF_DOC("Record a (partial) payment", "Billing")
    info->addConsumes<Object<dto::PaymentInputDto>>("application/json");
    info->addResponse<Object<dto::BillDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/bills/{id}/payments", pay, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::BILLING_MANAGE);
      return createDtoResponse(Status::CODE_200, m_svc->recordPayment(ctx, id, parse<Object<dto::PaymentInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(refund) {
    CF_DOC("Record a refund (reason required in notes)", "Billing")
    info->addConsumes<Object<dto::PaymentInputDto>>("application/json");
    info->addResponse<Object<dto::BillDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/bills/{id}/refunds", refund, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::BILLING_REFUND);
      return createDtoResponse(Status::CODE_200, m_svc->refund(ctx, id, parse<Object<dto::PaymentInputDto>>(bodyOf(request))));
    });
  }

  ENDPOINT_INFO(cancel) {
    CF_DOC("Cancel an unpaid bill (reason required)", "Billing")
    info->addConsumes<Object<dto::ReasonDto>>("application/json");
    info->addResponse<Object<dto::BillDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("POST", "/api/v1/bills/{id}/cancel", cancel, PATH(String, id), REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auto ctx = auth(request, perm::BILLING_REFUND);
      auto in = parse<Object<dto::ReasonDto>>(bodyOf(request));
      return createDtoResponse(Status::CODE_200, m_svc->cancel(ctx, id, in->reason ? *in->reason : ""));
    });
  }

  ENDPOINT_INFO(daily) {
    CF_DOC("Daily billing summary (date=YYYY-MM-DD, clinic timezone)", "Billing")
    info->addResponse<Object<dto::DailySummaryDto>>(Status::CODE_200, "application/json");
  }
  ENDPOINT("GET", "/api/v1/reports/billing/daily", daily, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return guard([&] {
      auth(request, perm::REPORT_BILLING);
      auto d = q(request, "date");
      if (!d) throw ApiError::badRequest("date is required");
      return createDtoResponse(Status::CODE_200, m_svc->dailySummary(*d));
    });
  }
};

}  // namespace cf

#include OATPP_CODEGEN_END(ApiController)
