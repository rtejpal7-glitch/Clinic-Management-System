#pragma once
// Thin, safe wrapper around libpq.
//  * Every query is parameterized (PQexecParams) — no string-built SQL with user input.
//  * A fixed-size connection pool with blocking checkout.
//  * Transactions via Db::tx([&](Conn& c){ ... }) with automatic rollback on exception.
//  * SQLSTATE-aware errors so services can map constraint violations to API errors.

#include <libpq-fe.h>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "oatpp/core/Types.hpp"

namespace cf {

class DbError : public std::runtime_error {
 public:
  std::string sqlstate;
  std::string constraint;
  DbError(const std::string& msg, std::string state, std::string constraintName)
      : std::runtime_error(msg), sqlstate(std::move(state)), constraint(std::move(constraintName)) {}
  bool isUniqueViolation() const { return sqlstate == "23505"; }
  bool isExclusionViolation() const { return sqlstate == "23P01"; }
  bool isForeignKeyViolation() const { return sqlstate == "23503"; }
  bool isCheckViolation() const { return sqlstate == "23514"; }
  bool isRaisedException() const { return sqlstate == "P0001"; }
};

// Query parameters. nullopt => SQL NULL.
class Params {
 public:
  std::vector<std::optional<std::string>> values;
  Params() = default;
  template <typename... Ts>
  explicit Params(Ts&&... vs) { (add(std::forward<Ts>(vs)), ...); }

  Params& add(const std::string& v) { values.emplace_back(v); return *this; }
  Params& add(const char* v) { if (v) values.emplace_back(std::string(v)); else values.emplace_back(std::nullopt); return *this; }
  Params& add(const oatpp::String& v) { if (v) values.emplace_back(std::string(*v)); else values.emplace_back(std::nullopt); return *this; }
  Params& add(const std::optional<std::string>& v) { values.push_back(v); return *this; }
  Params& add(int v) { values.emplace_back(std::to_string(v)); return *this; }
  Params& add(long v) { values.emplace_back(std::to_string(v)); return *this; }
  Params& add(long long v) { values.emplace_back(std::to_string(v)); return *this; }
  Params& add(bool v) { values.emplace_back(v ? "true" : "false"); return *this; }
  Params& add(const oatpp::Int32& v) { if (v) values.emplace_back(std::to_string(*v)); else values.emplace_back(std::nullopt); return *this; }
  Params& add(const oatpp::Int64& v) { if (v) values.emplace_back(std::to_string(*v)); else values.emplace_back(std::nullopt); return *this; }
  Params& add(const oatpp::Float64& v) { if (v) values.emplace_back(std::to_string(*v)); else values.emplace_back(std::nullopt); return *this; }
  Params& add(const oatpp::Boolean& v) { if (v) values.emplace_back(*v ? "true" : "false"); else values.emplace_back(std::nullopt); return *this; }
  Params& add(std::nullptr_t) { values.emplace_back(std::nullopt); return *this; }
};

class Result;

// A single row view. Column access by name; NULL-aware typed getters.
class Row {
 public:
  // Shares ownership of the result so a Row stays valid after its Result goes out of scope.
  Row(std::shared_ptr<PGresult> r, int i) : m_owner(std::move(r)), m_res(m_owner.get()), m_row(i) {}
  bool isNull(const char* col) const;
  std::optional<std::string> opt(const char* col) const;
  std::string str(const char* col, const std::string& def = "") const;
  long long i64v(const char* col, long long def = 0) const;
  bool boolv(const char* col, bool def = false) const;
  // Oat++ typed helpers (nullptr when SQL NULL)
  oatpp::String s(const char* col) const;
  oatpp::String ts(const char* col) const;  // timestamptz -> ISO-8601 UTC "YYYY-MM-DDTHH:MM:SS.mmmZ"
  oatpp::Int32 i32(const char* col) const;
  oatpp::Int64 i64(const char* col) const;
  oatpp::Float64 f64(const char* col) const;
  oatpp::Boolean b(const char* col) const;
  oatpp::Vector<oatpp::String> strArray(const char* col) const;  // text[] -> vector

 private:
  int col(const char* name) const;
  std::shared_ptr<PGresult> m_owner;
  PGresult* m_res;
  int m_row;
};

class Result {
 public:
  explicit Result(PGresult* r) : m_res(r, PQclear) {}
  int rows() const { return PQntuples(m_res.get()); }
  bool empty() const { return rows() == 0; }
  Row row(int i) const { return Row(m_res, i); }
  Row first() const;
  long long affected() const;
  template <typename F>
  void each(F f) const { for (int i = 0; i < rows(); ++i) f(row(i)); }

 private:
  std::shared_ptr<PGresult> m_res;
};

// Request-scoped tenant context. Each HTTP request is served on one thread, so the
// facility of the authenticated caller is held thread-locally and applied to every
// pooled connection the request checks out (set_config('app.facility_id')). The
// PostgreSQL RLS policies from V009 then restrict every clinical/financial table to
// that facility. Bypass is only for trusted cross-facility code paths (login,
// background jobs, de-identified network aggregation) and is always scoped.
struct DbContext {
  std::string facilityId;  // empty => no facility (RLS hides all tenant rows)
  bool bypass = false;
  static DbContext& current();
  static void clear() { current() = DbContext{}; }
};
class ScopedFacility {
 public:
  explicit ScopedFacility(std::string facilityId, bool bypass = false) : m_prev(DbContext::current()) {
    DbContext::current().facilityId = std::move(facilityId);
    DbContext::current().bypass = bypass;
  }
  ~ScopedFacility() { if (!m_committed) DbContext::current() = m_prev; }
  // Leave the scope with a new (non-bypass) facility context instead of restoring.
  void commit(const std::string& facilityId) {
    DbContext::current().facilityId = facilityId;
    DbContext::current().bypass = false;
    m_committed = true;
  }
  ScopedFacility(const ScopedFacility&) = delete;
  ScopedFacility& operator=(const ScopedFacility&) = delete;
 private:
  DbContext m_prev;
  bool m_committed = false;
};
struct ScopedRlsBypass : ScopedFacility {
  ScopedRlsBypass() : ScopedFacility(DbContext::current().facilityId, true) {}
};

class Conn {
 public:
  explicit Conn(PGconn* c) : m_conn(c) {}
  ~Conn();
  Conn(const Conn&) = delete;
  Conn& operator=(const Conn&) = delete;

  Result exec(const std::string& sql, const Params& params = Params()) const;
  void execRaw(const std::string& sql) const;  // multi-statement, no params (migrations only)
  bool healthy() const { return PQstatus(m_conn) == CONNECTION_OK; }
  PGconn* raw() const { return m_conn; }
  // Last tenant context applied to this session (avoids a round trip when unchanged)
  std::string appliedFacility = "\x01unset";
  bool appliedBypass = false;

 private:
  PGconn* m_conn;
};

class Db {
 public:
  Db(const std::string& url, int poolSize);
  static std::unique_ptr<Conn> connect(const std::string& url);

  template <typename F>
  auto withConn(F f) -> decltype(f(std::declval<Conn&>())) {
    auto lease = acquire();
    return f(*lease.conn);
  }

  // Run f inside a transaction; commit on success, rollback on any exception.
  template <typename F>
  auto tx(F f) -> decltype(f(std::declval<Conn&>())) {
    auto lease = acquire();
    Conn& c = *lease.conn;
    c.exec("BEGIN");
    try {
      if constexpr (std::is_void_v<decltype(f(c))>) {
        f(c);
        c.exec("COMMIT");
      } else {
        auto r = f(c);
        c.exec("COMMIT");
        return r;
      }
    } catch (...) {
      try { c.exec("ROLLBACK"); } catch (...) {}
      throw;
    }
  }

 private:
  struct Lease {
    Db* db;
    std::unique_ptr<Conn> conn;
    Lease(Db* d, std::unique_ptr<Conn> c) : db(d), conn(std::move(c)) {}
    Lease(Lease&& o) noexcept : db(o.db), conn(std::move(o.conn)) {}
    ~Lease() { if (conn) db->release(std::move(conn)); }
  };
  Lease acquire();
  void release(std::unique_ptr<Conn> c);

  std::string m_url;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<std::unique_ptr<Conn>> m_idle;
};

}  // namespace cf
