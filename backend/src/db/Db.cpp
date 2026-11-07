#include "Db.hpp"

#include <chrono>
#include <cstring>

#include "oatpp/core/base/Environment.hpp"

namespace cf {

// ---------------- Row ----------------
int Row::col(const char* name) const {
  int c = PQfnumber(m_res, name);
  if (c < 0) throw std::runtime_error(std::string("Unknown result column: ") + name);
  return c;
}
bool Row::isNull(const char* name) const { return PQgetisnull(m_res, m_row, col(name)) == 1; }
std::optional<std::string> Row::opt(const char* name) const {
  int c = col(name);
  if (PQgetisnull(m_res, m_row, c)) return std::nullopt;
  return std::string(PQgetvalue(m_res, m_row, c), PQgetlength(m_res, m_row, c));
}
std::string Row::str(const char* name, const std::string& def) const {
  auto v = opt(name);
  return v ? *v : def;
}
long long Row::i64v(const char* name, long long def) const {
  auto v = opt(name);
  return v ? std::stoll(*v) : def;
}
bool Row::boolv(const char* name, bool def) const {
  auto v = opt(name);
  return v ? (*v == "t" || *v == "true") : def;
}
oatpp::String Row::s(const char* name) const {
  auto v = opt(name);
  return v ? oatpp::String(*v) : oatpp::String(nullptr);
}
oatpp::String Row::ts(const char* name) const {
  // Sessions run with TimeZone=UTC and DateStyle=ISO, so values look like
  // "2026-11-07 15:11:55.123456+00". Normalise to RFC 3339 with milliseconds.
  auto v = opt(name);
  if (!v) return nullptr;
  std::string in = *v;
  if (in.size() < 19) return oatpp::String(in);
  std::string out = in.substr(0, 10) + "T" + in.substr(11, 8);
  std::string frac = "000";
  if (in.size() > 19 && in[19] == '.') {
    size_t j = 20;
    std::string digits;
    while (j < in.size() && isdigit(static_cast<unsigned char>(in[j]))) digits += in[j++];
    digits += "000";
    frac = digits.substr(0, 3);
  }
  return oatpp::String(out + "." + frac + "Z");
}
oatpp::Int32 Row::i32(const char* name) const {
  auto v = opt(name);
  return v ? oatpp::Int32(std::stoi(*v)) : oatpp::Int32(nullptr);
}
oatpp::Int64 Row::i64(const char* name) const {
  auto v = opt(name);
  return v ? oatpp::Int64(std::stoll(*v)) : oatpp::Int64(nullptr);
}
oatpp::Float64 Row::f64(const char* name) const {
  auto v = opt(name);
  return v ? oatpp::Float64(std::stod(*v)) : oatpp::Float64(nullptr);
}
oatpp::Boolean Row::b(const char* name) const {
  auto v = opt(name);
  return v ? oatpp::Boolean(*v == "t" || *v == "true") : oatpp::Boolean(nullptr);
}
oatpp::Vector<oatpp::String> Row::strArray(const char* name) const {
  // Parses simple PostgreSQL text[] literals: {a,b,"c d"}
  auto out = oatpp::Vector<oatpp::String>::createShared();
  auto v = opt(name);
  if (!v || v->size() < 2) return out;
  std::string s = v->substr(1, v->size() - 2);
  std::string cur;
  bool inQuotes = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char ch = s[i];
    if (ch == '"') { inQuotes = !inQuotes; continue; }
    if (ch == '\\' && i + 1 < s.size()) { cur += s[++i]; continue; }
    if (ch == ',' && !inQuotes) { out->push_back(cur); cur.clear(); continue; }
    cur += ch;
  }
  if (!s.empty()) out->push_back(cur);
  return out;
}

// ---------------- Result ----------------
Row Result::first() const {
  if (rows() == 0) throw std::runtime_error("Query returned no rows");
  return row(0);
}
long long Result::affected() const {
  const char* n = PQcmdTuples(m_res.get());
  return (n && *n) ? std::stoll(n) : 0;
}

// ---------------- Conn ----------------
Conn::~Conn() { if (m_conn) PQfinish(m_conn); }

static DbError makeError(PGresult* res, PGconn* conn) {
  std::string state, constraint, msg;
  if (res) {
    const char* s = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    const char* c = PQresultErrorField(res, PG_DIAG_CONSTRAINT_NAME);
    const char* m = PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY);
    state = s ? s : "";
    constraint = c ? c : "";
    msg = m ? m : PQresultErrorMessage(res);
  } else {
    msg = PQerrorMessage(conn);
  }
  return DbError(msg, state, constraint);
}

Result Conn::exec(const std::string& sql, const Params& params) const {
  std::vector<const char*> vals;
  vals.reserve(params.values.size());
  for (auto& v : params.values) vals.push_back(v ? v->c_str() : nullptr);
  PGresult* res = PQexecParams(m_conn, sql.c_str(), static_cast<int>(vals.size()), nullptr,
                               vals.empty() ? nullptr : vals.data(), nullptr, nullptr, 0);
  auto st = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
  if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
    DbError e = makeError(res, m_conn);
    if (res) PQclear(res);
    throw e;
  }
  return Result(res);
}

void Conn::execRaw(const std::string& sql) const {
  PGresult* res = PQexec(m_conn, sql.c_str());
  auto st = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
  if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
    DbError e = makeError(res, m_conn);
    if (res) PQclear(res);
    throw e;
  }
  PQclear(res);
}

// ---------------- Db ----------------
std::unique_ptr<Conn> Db::connect(const std::string& url) {
  PGconn* pc = PQconnectdb(url.c_str());
  if (PQstatus(pc) != CONNECTION_OK) {
    std::string msg = PQerrorMessage(pc);
    PQfinish(pc);
    throw std::runtime_error("Database connection failed: " + msg);
  }
  auto c = std::make_unique<Conn>(pc);
  // Deterministic session settings: UTC timestamps, ISO dates, bounded statements.
  c->exec("SET TimeZone = 'UTC'");
  c->exec("SET DateStyle = 'ISO, YMD'");
  c->exec("SET statement_timeout = '15s'");
  return c;
}

Db::Db(const std::string& url, int poolSize) : m_url(url) {
  for (int i = 0; i < poolSize; ++i) m_idle.push_back(connect(url));
}

Db::Lease Db::acquire() {
  std::unique_lock<std::mutex> lock(m_mutex);
  if (!m_cv.wait_for(lock, std::chrono::seconds(10), [&] { return !m_idle.empty(); })) {
    throw std::runtime_error("Database pool exhausted");
  }
  auto c = std::move(m_idle.back());
  m_idle.pop_back();
  lock.unlock();
  if (!c->healthy()) {
    PQreset(c->raw());
    if (!c->healthy()) c = connect(m_url);
  }
  return Lease(this, std::move(c));
}

void Db::release(std::unique_ptr<Conn> c) {
  // A connection returned mid-transaction (should not happen) is discarded.
  if (PQtransactionStatus(c->raw()) != PQTRANS_IDLE) {
    try { c->exec("ROLLBACK"); } catch (...) {}
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_idle.push_back(std::move(c));
  }
  m_cv.notify_one();
}

}  // namespace cf
