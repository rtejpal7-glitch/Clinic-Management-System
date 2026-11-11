#pragma once
// Extension point for specialty modules (dermatology today; others later).
// ClinicalService calls every registered extension inside the finalize transaction,
// so a module can validate (and block) finalization and capture structured data
// atomically with the locked clinical record.

#include <optional>
#include <string>

#include "db/Db.hpp"
#include "security/AuthContext.hpp"

namespace cf {

struct FinalizeOptions {
  std::optional<std::string> safetyOverrideReason;
};

class ConsultationExtension {
 public:
  virtual ~ConsultationExtension() = default;
  // Throw ApiError to refuse finalization (e.g. unacknowledged safety alerts).
  virtual void beforeFinalize(Conn& c, const AuthContext& ctx, const std::string& consultationId,
                              const FinalizeOptions& opts) = 0;
  // Runs after the consultation and prescription rows are locked, same transaction.
  virtual void afterFinalize(Conn& c, const AuthContext& ctx, const std::string& consultationId) = 0;
};

}  // namespace cf
