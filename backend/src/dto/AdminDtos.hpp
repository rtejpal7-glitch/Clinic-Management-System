#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

// ---------- Auth ----------
class LoginRequestDto : public oatpp::DTO {
  DTO_INIT(LoginRequestDto, DTO)
  DTO_FIELD(String, username);
  DTO_FIELD(String, password);
};

class CurrentUserDto : public oatpp::DTO {
  DTO_INIT(CurrentUserDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, username);
  DTO_FIELD(String, fullName);
  DTO_FIELD(Vector<String>, roles);
  DTO_FIELD(Vector<String>, permissions);
  DTO_FIELD(String, doctorId);
  DTO_FIELD(Boolean, mustChangePassword);
  DTO_FIELD(Int32, sessionIdleMinutes);
  DTO_FIELD(String, facilityId);
  DTO_FIELD(String, facilityCode);
  DTO_FIELD(String, facilityName);
  DTO_FIELD(String, specialty);
};

class LoginResponseDto : public oatpp::DTO {
  DTO_INIT(LoginResponseDto, DTO)
  DTO_FIELD(String, token);
  DTO_FIELD(String, expiresAt);
  DTO_FIELD(Object<CurrentUserDto>, user);
};

class ChangePasswordDto : public oatpp::DTO {
  DTO_INIT(ChangePasswordDto, DTO)
  DTO_FIELD(String, currentPassword);
  DTO_FIELD(String, newPassword);
};

// ---------- Users & roles ----------
class UserDto : public oatpp::DTO {
  DTO_INIT(UserDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, username);
  DTO_FIELD(String, fullName);
  DTO_FIELD(String, email);
  DTO_FIELD(String, phone);
  DTO_FIELD(Boolean, isActive);
  DTO_FIELD(Vector<String>, roles);
  DTO_FIELD(String, doctorId);
  DTO_FIELD(String, lastLoginAt);
  DTO_FIELD(String, lockedUntil);
  DTO_FIELD(String, createdAt);
};

class UserInputDto : public oatpp::DTO {
  DTO_INIT(UserInputDto, DTO)
  DTO_FIELD(String, username);
  DTO_FIELD(String, fullName);
  DTO_FIELD(String, email);
  DTO_FIELD(String, phone);
  DTO_FIELD(String, password);          // create only
  DTO_FIELD(Boolean, isActive);
  DTO_FIELD(Vector<String>, roles);     // role codes
};

class ResetPasswordDto : public oatpp::DTO {
  DTO_INIT(ResetPasswordDto, DTO)
  DTO_FIELD(String, newPassword);
};

class PermissionDto : public oatpp::DTO {
  DTO_INIT(PermissionDto, DTO)
  DTO_FIELD(String, code);
  DTO_FIELD(String, module);
  DTO_FIELD(String, description);
};

class RoleDto : public oatpp::DTO {
  DTO_INIT(RoleDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, code);
  DTO_FIELD(String, name);
  DTO_FIELD(String, description);
  DTO_FIELD(Boolean, isSystem);
  DTO_FIELD(Int32, userCount);
  DTO_FIELD(Vector<String>, permissions);
};

class RoleInputDto : public oatpp::DTO {
  DTO_INIT(RoleInputDto, DTO)
  DTO_FIELD(String, code);
  DTO_FIELD(String, name);
  DTO_FIELD(String, description);
  DTO_FIELD(Vector<String>, permissions);
};

// ---------- Settings ----------
class DuplicateRulesDto : public oatpp::DTO {
  DTO_INIT(DuplicateRulesDto, DTO)
  DTO_FIELD(Int32, threshold);
  DTO_FIELD(Float64, nameSimilarityMin);
  DTO_FIELD(Int32, weightMobile);
  DTO_FIELD(Int32, weightDateOfBirth);
  DTO_FIELD(Int32, weightName);
  DTO_FIELD(Int32, weightGender);
};

class SettingsDto : public oatpp::DTO {
  DTO_INIT(SettingsDto, DTO)
  DTO_FIELD(String, clinicName);
  DTO_FIELD(String, address);
  DTO_FIELD(String, city);
  DTO_FIELD(String, state);
  DTO_FIELD(String, pincode);
  DTO_FIELD(String, phone);
  DTO_FIELD(String, email);
  DTO_FIELD(String, registrationNumber);
  DTO_FIELD(String, gstin);
  DTO_FIELD(String, hfrId);
  DTO_FIELD(String, timezone);
  DTO_FIELD(String, patientIdPrefix);
  DTO_FIELD(String, invoicePrefix);
  DTO_FIELD(Int32, defaultSlotMinutes);
  DTO_FIELD(Int32, sessionIdleMinutes);
  DTO_FIELD(Object<DuplicateRulesDto>, duplicateRules);
  DTO_FIELD(String, updatedAt);
};

// ---------- Audit ----------
class AuditLogDto : public oatpp::DTO {
  DTO_INIT(AuditLogDto, DTO)
  DTO_FIELD(Int64, id);
  DTO_FIELD(String, occurredAt);
  DTO_FIELD(String, userId);
  DTO_FIELD(String, username);
  DTO_FIELD(String, roles);
  DTO_FIELD(String, action);
  DTO_FIELD(String, entityType);
  DTO_FIELD(String, entityId);
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, outcome);
  DTO_FIELD(String, ipAddress);
  DTO_FIELD(String, details);   // JSON text
  DTO_FIELD(String, hash);
};

class AuditPageDto : public oatpp::DTO {
  DTO_INIT(AuditPageDto, DTO)
  DTO_FIELD(Vector<Object<AuditLogDto>>, items);
  DTO_FIELD(Int64, total);
  DTO_FIELD(Int32, page);
  DTO_FIELD(Int32, size);
};

class AuditVerifyDto : public oatpp::DTO {
  DTO_INIT(AuditVerifyDto, DTO)
  DTO_FIELD(Boolean, valid);
  DTO_FIELD(Int64, checkedRecords);
  DTO_FIELD(Int64, firstInvalidId);
  DTO_FIELD(String, verifiedAt);
};

// ---------- Dashboard ----------
class DashboardDto : public oatpp::DTO {
  DTO_INIT(DashboardDto, DTO)
  DTO_FIELD(String, date);
  DTO_FIELD(Int64, totalPatients);
  DTO_FIELD(Int64, newPatientsToday);
  DTO_FIELD(Int64, appointmentsToday);
  DTO_FIELD(Int64, waitingNow);
  DTO_FIELD(Int64, inConsultationNow);
  DTO_FIELD(Int64, consultationsCompletedToday);
  DTO_FIELD(Int64, pendingPaymentsCount);       // null when caller lacks billing.read
  DTO_FIELD(Int64, pendingPaymentsPaise);
  DTO_FIELD(Int64, revenueTodayPaise);          // null when caller lacks report.billing
  DTO_FIELD(Vector<Int64>, revenueLast7DaysPaise);
  DTO_FIELD(Vector<String>, last7Days);
  DTO_FIELD(Fields<Int64>, appointmentsByStatus);
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
