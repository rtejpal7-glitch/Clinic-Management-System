#pragma once
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)

namespace cf::dto {

class ServiceDto : public oatpp::DTO {
  DTO_INIT(ServiceDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, code);
  DTO_FIELD(String, name);
  DTO_FIELD(String, category);
  DTO_FIELD(Int64, pricePaise);
  DTO_FIELD(Int32, taxRateBp);
  DTO_FIELD(Boolean, isActive);
};

class BillItemInputDto : public oatpp::DTO {
  DTO_INIT(BillItemInputDto, DTO)
  DTO_FIELD(String, serviceId);
  DTO_FIELD(String, description);      // required when serviceId is absent
  DTO_FIELD(Int32, quantity);
  DTO_FIELD(Int64, unitPricePaise);    // defaults to catalogue price
  DTO_FIELD(Int32, taxRateBp);         // defaults to catalogue rate
};

class BillCreateDto : public oatpp::DTO {
  DTO_INIT(BillCreateDto, DTO)
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, appointmentId);
  DTO_FIELD(String, consultationId);
  DTO_FIELD(Vector<Object<BillItemInputDto>>, items);
  DTO_FIELD(Int64, discountPaise);
  DTO_FIELD(String, discountReason);
  DTO_FIELD(String, notes);
};

class BillItemDto : public oatpp::DTO {
  DTO_INIT(BillItemDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, serviceId);
  DTO_FIELD(String, description);
  DTO_FIELD(Int32, quantity);
  DTO_FIELD(Int64, unitPricePaise);
  DTO_FIELD(Int32, taxRateBp);
  DTO_FIELD(Int64, lineSubtotalPaise);
  DTO_FIELD(Int64, lineDiscountPaise);
  DTO_FIELD(Int64, lineTaxPaise);
  DTO_FIELD(Int64, lineTotalPaise);
};

class PaymentDto : public oatpp::DTO {
  DTO_INIT(PaymentDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, paymentType);
  DTO_FIELD(Int64, amountPaise);
  DTO_FIELD(String, method);
  DTO_FIELD(String, reference);
  DTO_FIELD(String, notes);
  DTO_FIELD(String, receivedBy);
  DTO_FIELD(String, receivedAt);
};

class PaymentInputDto : public oatpp::DTO {
  DTO_INIT(PaymentInputDto, DTO)
  DTO_FIELD(Int64, amountPaise);
  DTO_FIELD(String, method);   // CASH | UPI | CARD | OTHER
  DTO_FIELD(String, reference);
  DTO_FIELD(String, notes);
};

class BillDto : public oatpp::DTO {
  DTO_INIT(BillDto, DTO)
  DTO_FIELD(String, id);
  DTO_FIELD(String, invoiceNumber);
  DTO_FIELD(String, patientId);
  DTO_FIELD(String, patientName);
  DTO_FIELD(String, patientNumber);
  DTO_FIELD(String, patientMobile);
  DTO_FIELD(String, appointmentId);
  DTO_FIELD(String, consultationId);
  DTO_FIELD(String, status);
  DTO_FIELD(Int64, subtotalPaise);
  DTO_FIELD(Int64, discountPaise);
  DTO_FIELD(Int64, taxPaise);
  DTO_FIELD(Int64, totalPaise);
  DTO_FIELD(Int64, paidPaise);
  DTO_FIELD(Int64, refundedPaise);
  DTO_FIELD(Int64, balancePaise);
  DTO_FIELD(String, discountReason);
  DTO_FIELD(String, notes);
  DTO_FIELD(String, cancelledReason);
  DTO_FIELD(String, createdBy);
  DTO_FIELD(String, createdAt);
  DTO_FIELD(Vector<Object<BillItemDto>>, items);
  DTO_FIELD(Vector<Object<PaymentDto>>, payments);
};

class BillPageDto : public oatpp::DTO {
  DTO_INIT(BillPageDto, DTO)
  DTO_FIELD(Vector<Object<BillDto>>, items);
  DTO_FIELD(Int64, total);
  DTO_FIELD(Int32, page);
  DTO_FIELD(Int32, size);
};

class DailySummaryDto : public oatpp::DTO {
  DTO_INIT(DailySummaryDto, DTO)
  DTO_FIELD(String, date);
  DTO_FIELD(Int64, billsIssued);
  DTO_FIELD(Int64, billedPaise);
  DTO_FIELD(Int64, discountPaise);
  DTO_FIELD(Int64, taxPaise);
  DTO_FIELD(Int64, collectedPaise);
  DTO_FIELD(Int64, refundedPaise);
  DTO_FIELD(Int64, netCollectedPaise);
  DTO_FIELD(Int64, outstandingPaise);
  DTO_FIELD(Fields<Int64>, collectedByMethod);
  DTO_FIELD(Fields<Int64>, billsByStatus);
};

}  // namespace cf::dto

#include OATPP_CODEGEN_END(DTO)
