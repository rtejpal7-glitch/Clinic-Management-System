#pragma once
// Extensible notification interface. Messages are written to an outbox table
// (in the same transaction as the business change) and delivered by a
// background dispatcher through a NotificationChannel implementation.
//
// The PoC ships LogNotificationChannel only: it records what would be sent and
// marks provider='MOCK_LOG'. Real SMS/WhatsApp/email providers implement the
// same interface (DLT-registered templates are required for SMS in India).

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "db/Db.hpp"

namespace cf {

struct OutboundMessage {
  std::string id, channel, recipient, templateCode;
  nlohmann::json payload;
};

struct DeliveryResult {
  bool ok;
  std::string provider;
  std::string error;
};

class NotificationChannel {
 public:
  virtual ~NotificationChannel() = default;
  virtual DeliveryResult send(const OutboundMessage& msg) = 0;
};

class LogNotificationChannel : public NotificationChannel {
 public:
  DeliveryResult send(const OutboundMessage& msg) override;
};

class NotificationService {
 public:
  NotificationService(std::shared_ptr<Db> db, std::shared_ptr<NotificationChannel> channel)
      : m_db(std::move(db)), m_channel(std::move(channel)) {}

  // Queue a reminder for an appointment (called inside the booking transaction).
  void queueAppointmentReminder(Conn& c, const std::string& appointmentId) const;
  void cancelForEntity(Conn& c, const std::string& entityType, const std::string& entityId) const;
  int dispatchDue();

  static std::string maskPhone(const std::string& p);

 private:
  std::shared_ptr<Db> m_db;
  std::shared_ptr<NotificationChannel> m_channel;
};

}  // namespace cf
