#include "BillingMath.hpp"

namespace cf {

static int64_t roundHalfUpDiv(int64_t num, int64_t den) { return (num + den / 2) / den; }

BillTotals computeBill(const std::vector<BillLineInput>& lines, int64_t discountPaise) {
  if (lines.empty()) throw std::invalid_argument("A bill needs at least one line");
  if (discountPaise < 0) throw std::invalid_argument("Discount cannot be negative");
  BillTotals t;
  for (auto& l : lines) {
    if (l.quantity <= 0 || l.unitPricePaise < 0 || l.taxRateBp < 0) throw std::invalid_argument("Invalid line");
    t.subtotal += l.quantity * l.unitPricePaise;
  }
  if (discountPaise > t.subtotal) throw std::invalid_argument("Discount cannot exceed subtotal");
  t.discount = discountPaise;

  int64_t allocated = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    BillLineResult r{};
    r.subtotal = lines[i].quantity * lines[i].unitPricePaise;
    if (i + 1 == lines.size()) {
      r.discount = discountPaise - allocated;
    } else {
      r.discount = t.subtotal == 0 ? 0 : (discountPaise * r.subtotal) / t.subtotal;  // floor
    }
    allocated += r.discount;
    r.tax = roundHalfUpDiv((r.subtotal - r.discount) * lines[i].taxRateBp, 10000);
    r.total = r.subtotal - r.discount + r.tax;
    t.tax += r.tax;
    t.lines.push_back(r);
  }
  t.total = t.subtotal - t.discount + t.tax;
  return t;
}

const char* paymentStatus(int64_t total, int64_t paid, int64_t refunded) {
  if (paid > 0 && refunded >= paid) return "REFUNDED";
  if (paid >= total) return "PAID";
  if (paid > 0) return "PARTIALLY_PAID";
  return "PENDING";
}

}  // namespace cf
