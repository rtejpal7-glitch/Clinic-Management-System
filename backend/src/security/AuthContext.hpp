#pragma once
// Identity of the caller for one request, resolved from the bearer token.

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cf {

struct RequestMeta {
  std::string ip;
  std::string userAgent;
};

struct AuthContext {
  std::string userId;
  std::string username;
  std::string fullName;
  std::string sessionId;
  std::vector<std::string> roles;
  std::set<std::string> permissions;
  std::optional<std::string> doctorId;  // set when the user has a doctor profile
  bool mustChangePassword = false;
  int sessionIdleMinutes = 30;
  RequestMeta meta;

  bool has(const std::string& perm) const { return permissions.count(perm) > 0; }
  bool isDoctor() const { return doctorId.has_value(); }
  std::string rolesJoined() const {
    std::string s;
    for (auto& r : roles) s += (s.empty() ? "" : ",") + r;
    return s;
  }
};

// Permission codes (must match V007 reference data)
namespace perm {
inline const std::string DASHBOARD_READ = "dashboard.read";
inline const std::string PATIENT_READ = "patient.read";
inline const std::string PATIENT_CREATE = "patient.create";
inline const std::string PATIENT_UPDATE = "patient.update";
inline const std::string PATIENT_HISTORY_READ = "patient.history.read";
inline const std::string APPOINTMENT_READ = "appointment.read";
inline const std::string APPOINTMENT_MANAGE = "appointment.manage";
inline const std::string QUEUE_MANAGE = "queue.manage";
inline const std::string SCHEDULE_READ = "schedule.read";
inline const std::string SCHEDULE_MANAGE = "schedule.manage";
inline const std::string DOCTOR_MANAGE = "doctor.manage";
inline const std::string CLINICAL_READ = "clinical.read";
inline const std::string CLINICAL_WRITE = "clinical.write";
inline const std::string CLINICAL_FINALIZE = "clinical.finalize";
inline const std::string PRESCRIPTION_WRITE = "prescription.write";
inline const std::string CATALOGUE_READ = "catalogue.read";
inline const std::string CATALOGUE_MANAGE = "catalogue.manage";
inline const std::string BILLING_READ = "billing.read";
inline const std::string BILLING_MANAGE = "billing.manage";
inline const std::string BILLING_REFUND = "billing.refund";
inline const std::string REPORT_BILLING = "report.billing";
inline const std::string USER_MANAGE = "user.manage";
inline const std::string ROLE_MANAGE = "role.manage";
inline const std::string SETTINGS_MANAGE = "settings.manage";
inline const std::string AUDIT_READ = "audit.read";
inline const std::string ABDM_READ = "abdm.read";
inline const std::string ABDM_LINK = "abdm.link";
inline const std::string ABDM_CONSENT = "abdm.consent";
inline const std::string FHIR_EXPORT = "fhir.export";
// Pseudo-permission: any authenticated user
inline const std::string AUTHENTICATED = "";
}  // namespace perm

}  // namespace cf
