#pragma once
// Outpatient billing (NABH CMS FPM.3 scope). Financial records are kept in
// separate tables from clinical records and only reference them by id.

#include <memory>
#include <optional>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "dto/BillingDtos.hpp"
#include "security/AuthContext.hpp"

namespace cf {

struct BillFilter {
  std::optional<std::string> date, status, patientId, q;
  int page = 1, size = 50;
};

class BillingService {
 public:
  BillingService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit)
      : m_db(std::move(db)), m_audit(std::move(audit)) {}

  oatpp::Vector<oatpp::Object<dto::ServiceDto>> listServices(bool includeInactive) const;
  oatpp::Object<dto::ServiceDto> saveService(const AuthContext& ctx, const std::string& id,
                                             const oatpp::Object<dto::ServiceDto>& in);

  oatpp::Object<dto::BillDto> create(const AuthContext& ctx, const oatpp::Object<dto::BillCreateDto>& in);
  oatpp::Object<dto::BillDto> get(const AuthContext& ctx, const std::string& id) const;
  oatpp::Object<dto::BillPageDto> list(const BillFilter& f) const;
  oatpp::Object<dto::BillDto> recordPayment(const AuthContext& ctx, const std::string& id,
                                            const oatpp::Object<dto::PaymentInputDto>& in);
  oatpp::Object<dto::BillDto> refund(const AuthContext& ctx, const std::string& id,
                                     const oatpp::Object<dto::PaymentInputDto>& in);
  oatpp::Object<dto::BillDto> cancel(const AuthContext& ctx, const std::string& id, const std::string& reason);
  oatpp::Object<dto::DailySummaryDto> dailySummary(const std::string& date) const;

 private:
  oatpp::Object<dto::BillDto> load(Conn& c, const std::string& id) const;
  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
};

}  // namespace cf
