#include "AuditService.hpp"

#include <algorithm>

namespace cf {

// The hash is computed inside PostgreSQL so the exact stored representation of
// occurred_at and details (jsonb canonical text) is what gets hashed; verify()
// recomputes it with the same expression.
static const char* HASH_EXPR =
    "encode(sha256(convert_to(concat_ws('|', %PREV%, "
    "to_char(%TS% AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"'), "
    "coalesce(%USER%::text, ''), %ACTION%, coalesce(%ETYPE%, ''), coalesce(%EID%, ''), %OUTCOME%, "
    "coalesce(%DETAILS%::text, '')), 'UTF8')), 'hex')";

static std::string hashExpr(const std::string& prev, const std::string& ts, const std::string& user,
                            const std::string& action, const std::string& etype, const std::string& eid,
                            const std::string& outcome, const std::string& details) {
  std::string s = HASH_EXPR;
  auto rep = [&](const std::string& key, const std::string& val) {
    size_t p;
    while ((p = s.find(key)) != std::string::npos) s.replace(p, key.size(), val);
  };
  rep("%PREV%", prev);
  rep("%TS%", ts);
  rep("%USER%", user);
  rep("%ACTION%", action);
  rep("%ETYPE%", etype);
  rep("%EID%", eid);
  rep("%OUTCOME%", outcome);
  rep("%DETAILS%", details);
  return s;
}

void AuditService::record(Conn& c, const AuditActor& actor, const AuditEvent& e) const {
  // Serialize chain appends (transaction-scoped lock, released at commit/rollback).
  c.exec("SELECT pg_advisory_xact_lock(727002)");
  static const std::string sql =
      "WITH prev AS (SELECT hash FROM audit_logs ORDER BY id DESC LIMIT 1), "
      "     v AS (SELECT clock_timestamp() AS ts, $1::uuid AS uid, $6::text AS act, $7::text AS etype, "
      "                  $8::text AS eid, $10::text AS outc, $11::jsonb AS det, "
      "                  coalesce((SELECT hash FROM prev), 'GENESIS') AS ph) "
      "INSERT INTO audit_logs (occurred_at, user_id, username, roles, ip_address, user_agent, action, entity_type, "
      "  entity_id, patient_id, outcome, details, prev_hash, hash) "
      "SELECT v.ts, v.uid, $2, $3, $4, $5, v.act, v.etype, v.eid, $9::uuid, v.outc, v.det, v.ph, " +
      hashExpr("v.ph", "v.ts", "v.uid", "v.act", "v.etype", "v.eid", "v.outc", "v.det") + " FROM v";
  Params p;
  p.add(actor.userId);
  p.add(actor.username);
  p.add(actor.roles);
  p.add(actor.meta.ip);
  p.add(actor.meta.userAgent.substr(0, 300));
  p.add(e.action);
  p.add(e.entityType.empty() ? std::optional<std::string>() : std::optional<std::string>(e.entityType));
  p.add(e.entityId.empty() ? std::optional<std::string>() : std::optional<std::string>(e.entityId));
  p.add(e.patientId);
  p.add(e.outcome);
  p.add(e.details.dump());
  c.exec(sql, p);
}

void AuditService::recordStandalone(const AuditActor& actor, const AuditEvent& e) const {
  m_db->tx([&](Conn& c) { record(c, actor, e); });
}

oatpp::Object<dto::AuditPageDto> AuditService::list(const AuditQuery& q) const {
  std::string where = q.includeInstallationLevel
                          ? " WHERE (facility_id = cf_current_facility() OR facility_id IS NULL)"
                          : " WHERE facility_id = cf_current_facility()";
  Params p;
  int n = 0;
  auto add = [&](const std::optional<std::string>& v, const std::string& clause) {
    if (v && !v->empty()) {
      p.add(*v);
      std::string c = clause;
      size_t pos = c.find('?');
      c.replace(pos, 1, "$" + std::to_string(++n));
      where += " AND " + c;
    }
  };
  add(q.from, "occurred_at >= ?::timestamptz");
  add(q.to, "occurred_at < ?::timestamptz");
  add(q.userId, "user_id = ?::uuid");
  add(q.action, "action = ?");
  add(q.entityType, "entity_type = ?");
  add(q.entityId, "entity_id = ?");
  add(q.patientId, "patient_id = ?::uuid");
  add(q.outcome, "outcome = ?");

  int size = std::clamp(q.size, 1, 200);
  int page = std::max(1, q.page);
  return m_db->withConn([&](Conn& c) {
    auto total = c.exec("SELECT count(*) AS n FROM audit_logs" + where, p).first().i64v("n");
    Params pp = p;
    pp.add(size);
    pp.add((page - 1) * size);
    auto rows = c.exec("SELECT id, occurred_at, user_id, username, roles, action, entity_type, entity_id, patient_id, "
                       "outcome, ip_address, details::text AS details, hash FROM audit_logs" + where +
                       " ORDER BY id DESC LIMIT $" + std::to_string(n + 1) + " OFFSET $" + std::to_string(n + 2),
                       pp);
    auto out = dto::AuditPageDto::createShared();
    out->items = oatpp::Vector<oatpp::Object<dto::AuditLogDto>>::createShared();
    rows.each([&](const Row& r) {
      auto d = dto::AuditLogDto::createShared();
      d->id = r.i64("id");
      d->occurredAt = r.ts("occurred_at");
      d->userId = r.s("user_id");
      d->username = r.s("username");
      d->roles = r.s("roles");
      d->action = r.s("action");
      d->entityType = r.s("entity_type");
      d->entityId = r.s("entity_id");
      d->patientId = r.s("patient_id");
      d->outcome = r.s("outcome");
      d->ipAddress = r.s("ip_address");
      d->details = r.s("details");
      d->hash = r.s("hash");
      out->items->push_back(d);
    });
    out->total = total;
    out->page = page;
    out->size = size;
    return out;
  });
}

oatpp::Object<dto::AuditVerifyDto> AuditService::verify() const {
  // Recompute every hash and compare with both the stored hash and the next
  // record's prev_hash. Returns the first broken record id, if any.
  static const std::string sql =
      "WITH chain AS ("
      "  SELECT id, hash, prev_hash, lag(hash) OVER (ORDER BY id) AS expected_prev, " +
      hashExpr("prev_hash", "occurred_at", "user_id", "action", "entity_type", "entity_id", "outcome", "details") +
      " AS recomputed FROM audit_logs) "
      "SELECT count(*) AS checked, "
      "  min(id) FILTER (WHERE hash <> recomputed OR prev_hash <> coalesce(expected_prev, 'GENESIS')) AS first_bad "
      "FROM chain";
  return m_db->withConn([&](Conn& c) {
    auto r = c.exec(sql).first();
    auto d = dto::AuditVerifyDto::createShared();
    d->checkedRecords = r.i64("checked");
    d->firstInvalidId = r.i64("first_bad");
    d->valid = r.isNull("first_bad");
    d->verifiedAt = c.exec("SELECT now() AS t").first().ts("t");
    return d;
  });
}

}  // namespace cf
