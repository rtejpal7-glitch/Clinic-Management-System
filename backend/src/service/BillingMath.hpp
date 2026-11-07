#pragma once
// Deterministic invoice arithmetic in integer paise (no floating point).
//  1. line subtotal = quantity x unit price
//  2. bill-level discount is allocated across lines in proportion to their
//     subtotal; the last line absorbs the rounding remainder so allocations sum
//     exactly to the discount
//  3. tax per line = round_half_up((line subtotal - line discount) x rate_bp / 10000)
//  4. total = subtotal - discount + tax

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cf {

struct BillLineInput {
  int64_t quantity;
  int64_t unitPricePaise;
  int32_t taxRateBp;
};

struct BillLineResult {
  int64_t subtotal;
  int64_t discount;
  int64_t tax;
  int64_t total;
};

struct BillTotals {
  std::vector<BillLineResult> lines;
  int64_t subtotal = 0;
  int64_t discount = 0;
  int64_t tax = 0;
  int64_t total = 0;
};

BillTotals computeBill(const std::vector<BillLineInput>& lines, int64_t discountPaise);

// Status after recording payments against a bill.
const char* paymentStatus(int64_t totalPaise, int64_t paidPaise, int64_t refundedPaise);

}  // namespace cf
