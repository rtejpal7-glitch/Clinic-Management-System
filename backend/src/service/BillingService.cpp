#include "BillingService.hpp"

#include <regex>

#include "common/ApiError.hpp"
#include "common/Text.hpp"
#include "service/BillingMath.hpp"

namespace cf {

using text::opt;
using text::trimmed;

static oatpp::Object<dto::ServiceDto> serviceFromRow(const Row& r) {
  auto s = dto::ServiceDto::createShared();
  s->id = r.s("id");
  s->code = r.s("code");
  s->name = r.s("name");
  s->category = r.s("category");
  s->pricePaise = r.i64("price_paise");
  s->taxRateBp = r.i32("tax_rate_bp");
  s->isActive = r.b("is_active");
  return s;
}

oatpp::Vector<oatpp::Object<dto::ServiceDto>> BillingService::listServices(bool includeInactive) const {
  return m_db->withConn([&](Conn& c) {
    auto out = oatpp::Vector<oatpp::Object<dto::ServiceDto>>::createShared();
    c.exec(std::string("SELECT * FROM service_catalogue ") + (includeInactive ? "" : "WHERE is_active ") + "ORDER BY category, name")
        .each([&](const Row& r) { out->push_back(serviceFromRow(r)); });
    return out;
  });
}

oatpp::Object<dto::ServiceDto> BillingService::saveService(const AuthContext& ctx, const std::string& id,
                                                           const oatpp::Object<dto::ServiceDto>& in) {
  Validator v;
  std::string code = text::upper(trimmed(in->code));
  std::string cat = text::upper(trimmed(in->category));
  v.check(std::regex_match(code, std::regex("^[A-Z0-9_-]{2,30}$")), "code", "2-30 chars: A-Z, 0-9, - or _");
  v.check(trimmed(in->name).size() >= 2, "name", "Name is required");
  v.check(text::oneOf(cat, {"CONSULTATION", "PROCEDURE", "INVESTIGATION", "OTHER"}), "category", "Select a category");
  v.check(in->pricePaise && *in->pricePaise >= 0 && *in->pricePaise <= 100000000, "pricePaise", "Price must be 0 or more");
  if (in->taxRateBp) v.check(*in->taxRateBp >= 0 && *in->taxRateBp <= 2800, "taxRateBp", "0% to 28%");
  v.throwIfAny();
  return m_db->tx([&](Conn& c) {
    try {
      Result r = id.empty()
                     ? c.exec("INSERT INTO service_catalogue (code, name, category, price_paise, tax_rate_bp) "
                              "VALUES ($1,$2,$3,$4,coalesce($5,0)) RETURNING *",
                              Params(code, trimmed(in->name), cat, in->pricePaise, in->taxRateBp))
                     : c.exec("UPDATE service_catalogue SET code=$2, name=$3, category=$4, price_paise=$5, "
                              "tax_rate_bp=coalesce($6, tax_rate_bp), is_active=coalesce($7, is_active) WHERE id=$1 RETURNING *",
                              Params(id, code, trimmed(in->name), cat, in->pricePaise, in->taxRateBp, in->isActive));
      if (r.empty()) throw ApiError::notFound("Service");
      auto s = serviceFromRow(r.first());
      m_audit->record(c, ctx, AuditEvent{id.empty() ? "SERVICE_CREATE" : "SERVICE_UPDATE", "service", *s->id, std::nullopt,
                                         "SUCCESS", {{"code", code}, {"pricePaise", (long long)*s->pricePaise}}});
      return s;
    } catch (const DbError& e) {
      if (e.isUniqueViolation()) throw ApiError::conflict("CODE_EXISTS", "Service code already exists");
      throw;
    }
  });
}

static const char* BILL_SELECT =
    "SELECT b.*, p.full_name AS patient_name, p.patient_number, p.mobile AS patient_mobile, u.full_name AS created_by_name "
    "FROM bills b JOIN patients p ON p.id = b.patient_id LEFT JOIN users u ON u.id = b.created_by ";

static oatpp::Object<dto::BillDto> billFromRow(const Row& r) {
  auto b = dto::BillDto::createShared();
  b->id = r.s("id");
  b->invoiceNumber = r.s("invoice_number");
  b->patientId = r.s("patient_id");
  b->patientName = r.s("patient_name");
  b->patientNumber = r.s("patient_number");
  b->patientMobile = r.s("patient_mobile");
  b->appointmentId = r.s("appointment_id");
  b->consultationId = r.s("consultation_id");
  b->status = r.s("status");
  b->subtotalPaise = r.i64("subtotal_paise");
  b->discountPaise = r.i64("discount_paise");
  b->taxPaise = r.i64("tax_paise");
  b->totalPaise = r.i64("total_paise");
  b->paidPaise = r.i64("paid_paise");
  b->refundedPaise = r.i64("refunded_paise");
  b->balancePaise = r.str("status") == "CANCELLED" ? 0 : (*b->totalPaise - *b->paidPaise);
  b->discountReason = r.s("discount_reason");
  b->notes = r.s("notes");
  b->cancelledReason = r.s("cancelled_reason");
  b->createdBy = r.s("created_by_name");
  b->createdAt = r.ts("created_at");
  return b;
}

oatpp::Object<dto::BillDto> BillingService::load(Conn& c, const std::string& id) const {
  if (!text::isUuid(id)) throw ApiError::notFound("Bill");
  auto r = c.exec(std::string(BILL_SELECT) + "WHERE b.id = $1", Params(id));
  if (r.empty()) throw ApiError::notFound("Bill");
  auto b = billFromRow(r.first());
  b->items = oatpp::Vector<oatpp::Object<dto::BillItemDto>>::createShared();
  c.exec("SELECT * FROM bill_items WHERE bill_id = $1 ORDER BY sort_order", Params(id)).each([&](const Row& x) {
    auto i = dto::BillItemDto::createShared();
    i->id = x.s("id");
    i->serviceId = x.s("service_id");
    i->description = x.s("description");
    i->quantity = x.i32("quantity");
    i->unitPricePaise = x.i64("unit_price_paise");
    i->taxRateBp = x.i32("tax_rate_bp");
    i->lineSubtotalPaise = x.i64("line_subtotal_paise");
    i->lineDiscountPaise = x.i64("line_discount_paise");
    i->lineTaxPaise = x.i64("line_tax_paise");
    i->lineTotalPaise = x.i64("line_total_paise");
    b->items->push_back(i);
  });
  b->payments = oatpp::Vector<oatpp::Object<dto::PaymentDto>>::createShared();
  c.exec("SELECT p.*, u.full_name AS by_name FROM payments p LEFT JOIN users u ON u.id = p.received_by "
         "WHERE p.bill_id = $1 ORDER BY p.received_at", Params(id))
      .each([&](const Row& x) {
        auto p = dto::PaymentDto::createShared();
        p->id = x.s("id");
        p->paymentType = x.s("payment_type");
        p->amountPaise = x.i64("amount_paise");
        p->method = x.s("method");
        p->reference = x.s("reference");
        p->notes = x.s("notes");
        p->receivedBy = x.s("by_name");
        p->receivedAt = x.ts("received_at");
        b->payments->push_back(p);
      });
  return b;
}

oatpp::Object<dto::BillDto> BillingService::create(const AuthContext& ctx, const oatpp::Object<dto::BillCreateDto>& in) {
  Validator v;
  std::string patientId = trimmed(in->patientId);
  auto apptId = opt(in->appointmentId);
  auto consId = opt(in->consultationId);
  int64_t discount = in->discountPaise ? *in->discountPaise : 0;
  v.check(text::isUuid(patientId), "patientId", "Select a patient");
  if (apptId) v.check(text::isUuid(*apptId), "appointmentId", "Invalid appointment");
  if (consId) v.check(text::isUuid(*consId), "consultationId", "Invalid consultation");
  v.check(in->items && !in->items->empty(), "items", "Add at least one item");
  if (in->items) v.check(in->items->size() <= 50, "items", "At most 50 items");
  v.check(discount >= 0, "discountPaise", "Discount cannot be negative");
  if (discount > 0) v.check(trimmed(in->discountReason).size() >= 3, "discountReason", "A reason is required for discounts");
  v.throwIfAny();

  return m_db->tx([&](Conn& c) {
    if (c.exec("SELECT 1 FROM patients WHERE id = $1", Params(patientId)).empty()) throw ApiError::notFound("Patient");
    if (apptId) {
      auto a = c.exec("SELECT patient_id FROM appointments WHERE id = $1 FOR UPDATE", Params(*apptId));
      if (a.empty() || a.first().str("patient_id") != patientId)
        throw ApiError::badRequest("Appointment does not belong to this patient", {{"appointmentId", "Mismatch"}});
      if (!c.exec("SELECT 1 FROM bills WHERE appointment_id = $1 AND status <> 'CANCELLED'", Params(*apptId)).empty())
        throw ApiError::conflict("ALREADY_BILLED", "This appointment already has a bill");
    }
    if (consId) {
      auto cr = c.exec("SELECT patient_id FROM consultations WHERE id = $1", Params(*consId));
      if (cr.empty() || cr.first().str("patient_id") != patientId)
        throw ApiError::badRequest("Consultation does not belong to this patient", {{"consultationId", "Mismatch"}});
    }

    struct Line { std::optional<std::string> serviceId; std::string desc; BillLineInput in; };
    std::vector<Line> lines;
    Validator lv;
    int idx = 0;
    for (auto& it : *in->items) {
      std::string k = "items[" + std::to_string(idx++) + "]";
      Line l;
      l.in.quantity = it->quantity ? *it->quantity : 1;
      lv.check(l.in.quantity >= 1 && l.in.quantity <= 1000, k + ".quantity", "1 to 1000");
      if (it->serviceId && text::isUuid(*it->serviceId)) {
        auto s = c.exec("SELECT name, price_paise, tax_rate_bp FROM service_catalogue WHERE id = $1 AND is_active",
                        Params(std::string(*it->serviceId)));
        if (s.empty()) { lv.fail(k + ".serviceId", "Unknown or inactive service"); continue; }
        auto sr = s.first();
        l.serviceId = std::string(*it->serviceId);
        l.desc = opt(it->description).value_or(sr.str("name"));
        l.in.unitPricePaise = it->unitPricePaise ? *it->unitPricePaise : sr.i64v("price_paise");
        l.in.taxRateBp = it->taxRateBp ? *it->taxRateBp : static_cast<int32_t>(sr.i64v("tax_rate_bp"));
      } else {
        l.desc = trimmed(it->description);
        lv.check(l.desc.size() >= 2, k + ".description", "Description is required");
        lv.check(it->unitPricePaise != nullptr, k + ".unitPricePaise", "Price is required");
        l.in.unitPricePaise = it->unitPricePaise ? *it->unitPricePaise : 0;
        l.in.taxRateBp = it->taxRateBp ? *it->taxRateBp : 0;
      }
      lv.check(l.in.unitPricePaise >= 0 && l.in.unitPricePaise <= 100000000, k + ".unitPricePaise", "Invalid price");
      lv.check(l.in.taxRateBp >= 0 && l.in.taxRateBp <= 2800, k + ".taxRateBp", "0% to 28%");
      lines.push_back(l);
    }
    lv.throwIfAny();
    std::vector<BillLineInput> inputs;
    for (auto& l : lines) inputs.push_back(l.in);
    BillTotals totals;
    try {
      totals = computeBill(inputs, discount);
    } catch (const std::invalid_argument& e) {
      throw ApiError::badRequest(e.what(), {{"discountPaise", e.what()}});
    }

    auto year = c.exec("SELECT invoice_prefix, extract(year FROM now() AT TIME ZONE timezone)::int AS y FROM clinic_settings WHERE id = 1").first();
    long long seq = c.exec("INSERT INTO id_counters (counter_name, period, last_value) VALUES ('invoice', $1, 1) "
                           "ON CONFLICT (counter_name, period) DO UPDATE SET last_value = id_counters.last_value + 1 RETURNING last_value",
                           Params(year.str("y")))
                        .first()
                        .i64v("last_value");
    char num[16];
    snprintf(num, sizeof(num), "%06lld", seq);
    std::string invoice = year.str("invoice_prefix") + "-" + year.str("y") + "-" + num;
    const char* status = paymentStatus(totals.total, 0, 0);
    if (totals.total == 0) status = "PAID";

    auto billId = c.exec("INSERT INTO bills (invoice_number, patient_id, appointment_id, consultation_id, status, subtotal_paise, "
                         "discount_paise, tax_paise, total_paise, discount_reason, notes, created_by) "
                         "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12) RETURNING id",
                         Params(invoice, patientId, apptId, consId, std::string(status), (long long)totals.subtotal,
                                (long long)totals.discount, (long long)totals.tax, (long long)totals.total,
                                opt(in->discountReason), opt(in->notes), ctx.userId))
                      .first()
                      .str("id");
    for (size_t i = 0; i < lines.size(); ++i) {
      auto& l = lines[i];
      auto& r = totals.lines[i];
      c.exec("INSERT INTO bill_items (bill_id, service_id, description, quantity, unit_price_paise, tax_rate_bp, line_subtotal_paise, "
             "line_discount_paise, line_tax_paise, line_total_paise, sort_order) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11)",
             Params(billId, l.serviceId, l.desc, (long long)l.in.quantity, (long long)l.in.unitPricePaise, (int)l.in.taxRateBp,
                    (long long)r.subtotal, (long long)r.discount, (long long)r.tax, (long long)r.total, (int)i));
    }
    m_audit->record(c, ctx, AuditEvent{"BILL_CREATE", "bill", billId, patientId, "SUCCESS",
                                       {{"invoiceNumber", invoice}, {"totalPaise", (long long)totals.total},
                                        {"discountPaise", (long long)totals.discount}}});
    return load(c, billId);
  });
}

oatpp::Object<dto::BillDto> BillingService::get(const AuthContext& ctx, const std::string& id) const {
  return m_db->withConn([&](Conn& c) { return load(c, id); });
}

oatpp::Object<dto::BillPageDto> BillingService::list(const BillFilter& f) const {
  std::string where = "WHERE 1=1";
  Params p;
  int n = 0;
  auto add = [&](const std::optional<std::string>& v, const std::string& clause) {
    if (!v) return;
    p.add(*v);
    std::string cl = clause;
    size_t pos;
    while ((pos = cl.find('?')) != std::string::npos) cl.replace(pos, 1, "$" + std::to_string(n + 1));
    ++n;
    where += " AND " + cl;
  };
  if (f.date && !text::isIsoDate(*f.date)) throw ApiError::badRequest("date must be YYYY-MM-DD");
  add(f.date, "(b.created_at AT TIME ZONE (SELECT timezone FROM clinic_settings WHERE id = 1))::date = ?::date");
  add(f.status, "b.status = ?");
  add(f.patientId, "b.patient_id = ?::uuid");
  add(f.q, "(upper(b.invoice_number) LIKE '%' || upper(?) || '%' OR lower(p.full_name) LIKE '%' || lower(?) || '%')");
  int size = std::clamp(f.size, 1, 200), page = std::max(1, f.page);
  return m_db->withConn([&](Conn& c) {
    auto total = c.exec("SELECT count(*) AS n FROM bills b JOIN patients p ON p.id = b.patient_id " + where, p).first().i64v("n");
    Params pp = p;
    pp.add(size);
    pp.add((page - 1) * size);
    auto out = dto::BillPageDto::createShared();
    out->items = oatpp::Vector<oatpp::Object<dto::BillDto>>::createShared();
    c.exec(std::string(BILL_SELECT) + where + " ORDER BY b.created_at DESC LIMIT $" + std::to_string(n + 1) + " OFFSET $" +
               std::to_string(n + 2), pp)
        .each([&](const Row& r) { out->items->push_back(billFromRow(r)); });
    out->total = total;
    out->page = page;
    out->size = size;
    return out;
  });
}

static void validatePayment(const oatpp::Object<dto::PaymentInputDto>& in) {
  Validator v;
  v.check(in->amountPaise && *in->amountPaise > 0, "amountPaise", "Amount must be greater than zero");
  v.check(text::oneOf(text::upper(trimmed(in->method)), {"CASH", "UPI", "CARD", "OTHER"}), "method", "Select a payment method");
  if (in->reference) v.check(in->reference->size() <= 100, "reference", "Too long");
  v.throwIfAny();
}

oatpp::Object<dto::BillDto> BillingService::recordPayment(const AuthContext& ctx, const std::string& id,
                                                          const oatpp::Object<dto::PaymentInputDto>& in) {
  validatePayment(in);
  if (!text::isUuid(id)) throw ApiError::notFound("Bill");
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("SELECT * FROM bills WHERE id = $1 FOR UPDATE", Params(id));
    if (r.empty()) throw ApiError::notFound("Bill");
    auto b = r.first();
    std::string st = b.str("status");
    if (st == "CANCELLED" || st == "REFUNDED") throw ApiError::conflict("INVALID_STATE", "Payments cannot be recorded on a " + st + " bill");
    int64_t total = b.i64v("total_paise"), paid = b.i64v("paid_paise");
    int64_t amount = *in->amountPaise;
    if (amount > total - paid)
      throw ApiError::badRequest("Amount exceeds the outstanding balance", {{"amountPaise", "Maximum is the balance due"}});
    std::string method = text::upper(trimmed(in->method));
    auto pid = c.exec("INSERT INTO payments (bill_id, payment_type, amount_paise, method, reference, notes, received_by) "
                      "VALUES ($1, 'PAYMENT', $2, $3, $4, $5, $6) RETURNING id",
                      Params(id, (long long)amount, method, opt(in->reference), opt(in->notes), ctx.userId))
                   .first()
                   .str("id");
    int64_t newPaid = paid + amount;
    c.exec("UPDATE bills SET paid_paise = $2, status = $3 WHERE id = $1",
           Params(id, (long long)newPaid, std::string(paymentStatus(total, newPaid, b.i64v("refunded_paise")))));
    m_audit->record(c, ctx, AuditEvent{"PAYMENT_RECORD", "bill", id, b.str("patient_id"), "SUCCESS",
                                       {{"paymentId", pid}, {"amountPaise", (long long)amount}, {"method", method}}});
    return load(c, id);
  });
}

oatpp::Object<dto::BillDto> BillingService::refund(const AuthContext& ctx, const std::string& id,
                                                   const oatpp::Object<dto::PaymentInputDto>& in) {
  validatePayment(in);
  if (trimmed(in->notes).size() < 3) throw ApiError::badRequest("A refund reason is required", {{"notes", "Reason required"}});
  if (!text::isUuid(id)) throw ApiError::notFound("Bill");
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("SELECT * FROM bills WHERE id = $1 FOR UPDATE", Params(id));
    if (r.empty()) throw ApiError::notFound("Bill");
    auto b = r.first();
    int64_t paid = b.i64v("paid_paise"), refunded = b.i64v("refunded_paise"), amount = *in->amountPaise;
    if (amount > paid - refunded)
      throw ApiError::badRequest("Refund exceeds the amount collected", {{"amountPaise", "Maximum is the net amount collected"}});
    std::string method = text::upper(trimmed(in->method));
    auto pid = c.exec("INSERT INTO payments (bill_id, payment_type, amount_paise, method, reference, notes, received_by) "
                      "VALUES ($1, 'REFUND', $2, $3, $4, $5, $6) RETURNING id",
                      Params(id, (long long)amount, method, opt(in->reference), opt(in->notes), ctx.userId))
                   .first()
                   .str("id");
    int64_t newRefunded = refunded + amount;
    c.exec("UPDATE bills SET refunded_paise = $2, status = $3 WHERE id = $1",
           Params(id, (long long)newRefunded, std::string(paymentStatus(b.i64v("total_paise"), paid, newRefunded))));
    m_audit->record(c, ctx, AuditEvent{"PAYMENT_REFUND", "bill", id, b.str("patient_id"), "SUCCESS",
                                       {{"paymentId", pid}, {"amountPaise", (long long)amount}, {"reason", trimmed(in->notes)}}});
    return load(c, id);
  });
}

oatpp::Object<dto::BillDto> BillingService::cancel(const AuthContext& ctx, const std::string& id, const std::string& reason) {
  std::string rs = text::trim(reason);
  if (rs.size() < 3) throw ApiError::badRequest("A cancellation reason is required", {{"reason", "Required"}});
  if (!text::isUuid(id)) throw ApiError::notFound("Bill");
  return m_db->tx([&](Conn& c) {
    auto r = c.exec("SELECT status, paid_paise, patient_id FROM bills WHERE id = $1 FOR UPDATE", Params(id));
    if (r.empty()) throw ApiError::notFound("Bill");
    if (r.first().i64v("paid_paise") > 0)
      throw ApiError::conflict("HAS_PAYMENTS", "Bills with payments cannot be cancelled; record a refund instead");
    if (r.first().str("status") == "CANCELLED") throw ApiError::conflict("INVALID_STATE", "Bill is already cancelled");
    c.exec("UPDATE bills SET status = 'CANCELLED', cancelled_reason = $2 WHERE id = $1", Params(id, rs));
    m_audit->record(c, ctx, AuditEvent{"BILL_CANCEL", "bill", id, r.first().str("patient_id"), "SUCCESS", {{"reason", rs}}});
    return load(c, id);
  });
}

oatpp::Object<dto::DailySummaryDto> BillingService::dailySummary(const std::string& date) const {
  if (!text::isIsoDate(date)) throw ApiError::badRequest("date must be YYYY-MM-DD");
  return m_db->withConn([&](Conn& c) {
    auto d = dto::DailySummaryDto::createShared();
    d->date = date;
    auto r = c.exec(
        "WITH tz AS (SELECT timezone AS z FROM clinic_settings WHERE id = 1) "
        "SELECT count(*) FILTER (WHERE status <> 'CANCELLED') AS issued, "
        "  coalesce(sum(total_paise) FILTER (WHERE status <> 'CANCELLED'), 0) AS billed, "
        "  coalesce(sum(discount_paise) FILTER (WHERE status <> 'CANCELLED'), 0) AS discount, "
        "  coalesce(sum(tax_paise) FILTER (WHERE status <> 'CANCELLED'), 0) AS tax, "
        "  coalesce(sum(total_paise - paid_paise) FILTER (WHERE status IN ('PENDING','PARTIALLY_PAID')), 0) AS outstanding "
        "FROM bills WHERE (created_at AT TIME ZONE (SELECT z FROM tz))::date = $1::date",
        Params(date)).first();
    d->billsIssued = r.i64("issued");
    d->billedPaise = r.i64("billed");
    d->discountPaise = r.i64("discount");
    d->taxPaise = r.i64("tax");
    d->outstandingPaise = r.i64("outstanding");
    d->collectedByMethod = oatpp::Fields<oatpp::Int64>::createShared();
    int64_t collected = 0, refunded = 0;
    c.exec("WITH tz AS (SELECT timezone AS z FROM clinic_settings WHERE id = 1) "
           "SELECT method, payment_type, sum(amount_paise) AS amt FROM payments "
           "WHERE (received_at AT TIME ZONE (SELECT z FROM tz))::date = $1::date GROUP BY method, payment_type ORDER BY method",
           Params(date))
        .each([&](const Row& x) {
          int64_t amt = x.i64v("amt");
          if (x.str("payment_type") == "PAYMENT") {
            collected += amt;
            d->collectedByMethod->push_back({x.s("method"), oatpp::Int64(amt)});
          } else {
            refunded += amt;
          }
        });
    d->collectedPaise = collected;
    d->refundedPaise = refunded;
    d->netCollectedPaise = collected - refunded;
    d->billsByStatus = oatpp::Fields<oatpp::Int64>::createShared();
    c.exec("WITH tz AS (SELECT timezone AS z FROM clinic_settings WHERE id = 1) "
           "SELECT status, count(*) AS n FROM bills WHERE (created_at AT TIME ZONE (SELECT z FROM tz))::date = $1::date GROUP BY status",
           Params(date))
        .each([&](const Row& x) { d->billsByStatus->push_back({x.s("status"), x.i64("n")}); });
    return d;
  });
}

}  // namespace cf
