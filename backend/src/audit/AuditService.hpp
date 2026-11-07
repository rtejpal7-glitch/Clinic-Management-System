#pragma once
// Append-only, hash-chained audit trail.
//
// Each record stores hash = SHA-256(prev_hash | occurred_at | user | action |
// entity | outcome | details). Any edit or deletion of a historical row (e.g.
// by someone with direct DB access who bypasses the triggers) breaks the chain,
// which /audit-logs/verify detects.
//
// Business writes call record(conn, ...) inside the same transaction as the
// change, so an action and its audit entry commit or roll back together.

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "db/Db.hpp"
#include "dto/AdminDtos.hpp"
#include "security/AuthContext.hpp"

namespace cf {

struct AuditEvent {
  std::string action;
  std::string entityType;
  std::string entityId;
  std::optional<std::string> patientId;
  std::string outcome = "SUCCESS";
  nlohmann::json details = nlohmann::json::object();
};

struct AuditActor {
  std::optional<std::string> userId;
  std::string username;
  std::string roles;
  RequestMeta meta;
  static AuditActor from(const AuthContext& ctx) {
    return AuditActor{ctx.userId, ctx.username, ctx.rolesJoined(), ctx.meta};
  }
  static AuditActor anonymous(const std::string& attemptedUsername, const RequestMeta& meta) {
    return AuditActor{std::nullopt, attemptedUsername, "", meta};
  }
};

struct AuditQuery {
  std::optional<std::string> from, to, userId, action, entityType, entityId, patientId, outcome;
  int page = 1;
  int size = 50;
};

class AuditService {
 public:
  explicit AuditService(std::shared_ptr<Db> db) : m_db(std::move(db)) {}

  // Within an existing transaction.
  void record(Conn& c, const AuditActor& actor, const AuditEvent& e) const;
  void record(Conn& c, const AuthContext& ctx, const AuditEvent& e) const { record(c, AuditActor::from(ctx), e); }
  // Self-contained transaction (reads, denials, auth events).
  void recordStandalone(const AuditActor& actor, const AuditEvent& e) const;

  oatpp::Object<dto::AuditPageDto> list(const AuditQuery& q) const;
  oatpp::Object<dto::AuditVerifyDto> verify() const;

 private:
  std::shared_ptr<Db> m_db;
};

}  // namespace cf
