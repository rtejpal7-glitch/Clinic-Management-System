#pragma once
// Clinics (facilities) hosted on one ClinicFlow installation, and each clinic's
// participation in the de-identified ClinicFlow prescribing network.

#include <functional>
#include <memory>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "dto/FacilityDtos.hpp"
#include "security/AuthContext.hpp"

namespace cf {

inline const std::string DEFAULT_FACILITY_ID = "00000000-0000-4000-8000-000000000001";

class FacilityService {
 public:
  FacilityService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit)
      : m_db(std::move(db)), m_audit(std::move(audit)) {}

  // Platform administrators see every clinic; everyone else sees only their own.
  oatpp::Vector<oatpp::Object<dto::FacilityDto>> list(const AuthContext& ctx) const;
  oatpp::Object<dto::FacilityDto> current(const AuthContext& ctx) const;
  oatpp::Object<dto::FacilityDto> create(const AuthContext& ctx, const oatpp::Object<dto::FacilityCreateDto>& in);
  oatpp::Object<dto::FacilityDto> setNetworkSharing(const AuthContext& ctx, const oatpp::Object<dto::NetworkSharingDto>& in);

 // Called (inside the transaction) when a clinic leaves the network, to withdraw its records.
  void onNetworkLeave(std::function<void(Conn&, const std::string&)> fn) { m_onLeave = std::move(fn); }

 private:
  std::function<void(Conn&, const std::string&)> m_onLeave;
  oatpp::Object<dto::FacilityDto> get(Conn& c, const std::string& id) const;
  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
};

}  // namespace cf
