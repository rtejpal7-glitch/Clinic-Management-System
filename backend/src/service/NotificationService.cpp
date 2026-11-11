#include "NotificationService.hpp"

#include "oatpp/core/base/Environment.hpp"

namespace cf {

std::string NotificationService::maskPhone(const std::string& p) {
  if (p.size() < 4) return "****";
  return std::string(p.size() - 4, 'x') + p.substr(p.size() - 4);
}

DeliveryResult LogNotificationChannel::send(const OutboundMessage& msg) {
  // Logs metadata only — never message content containing patient details.
  OATPP_LOGI("Notify", "[MOCK %s] template=%s to=%s id=%s", msg.channel.c_str(), msg.templateCode.c_str(),
             NotificationService::maskPhone(msg.recipient).c_str(), msg.id.c_str());
  return {true, "MOCK_LOG", ""};
}

void NotificationService::queueAppointmentReminder(Conn& c, const std::string& appointmentId) const {
  // Reminder 24h before the appointment, or 2h before if booked inside 24h.
  // Skipped when the appointment starts within the next hour.
  c.exec(
      "INSERT INTO notifications (channel, recipient, template_code, payload, entity_type, entity_id, scheduled_for, facility_id) "
      "SELECT 'SMS', p.mobile, 'APPOINTMENT_REMINDER', "
      "  jsonb_build_object('patientFirstName', split_part(p.full_name, ' ', 1), 'doctorName', d.display_name, "
      "    'startAt', to_char(a.start_at AT TIME ZONE s.timezone, 'DD Mon YYYY HH12:MI AM'), 'clinicName', f.name), "
      "  'appointment', a.id, "
      "  CASE WHEN a.start_at - now() > interval '24 hours' THEN a.start_at - interval '24 hours' "
      "       ELSE a.start_at - interval '2 hours' END, a.facility_id "
      "FROM appointments a JOIN patients p ON p.id = a.patient_id JOIN doctors d ON d.id = a.doctor_id "
      "JOIN facilities f ON f.id = a.facility_id CROSS JOIN clinic_settings s "
      "WHERE a.id = $1 AND s.id = 1 AND a.start_at - now() > interval '1 hour'",
      Params(appointmentId));
}

void NotificationService::cancelForEntity(Conn& c, const std::string& entityType, const std::string& entityId) const {
  c.exec("UPDATE notifications SET status = 'CANCELLED' WHERE entity_type = $1 AND entity_id = $2 AND status = 'PENDING'",
         Params(entityType, entityId));
}

int NotificationService::dispatchDue() {
  return m_db->tx([&](Conn& c) {
    auto due = c.exec("SELECT id, channel, recipient, template_code, payload::text AS payload FROM notifications "
                      "WHERE status = 'PENDING' AND scheduled_for <= now() ORDER BY scheduled_for LIMIT 50 "
                      "FOR UPDATE SKIP LOCKED");
    int sent = 0;
    due.each([&](const Row& r) {
      OutboundMessage m{r.str("id"), r.str("channel"), r.str("recipient"), r.str("template_code"),
                        nlohmann::json::parse(r.str("payload", "{}"))};
      DeliveryResult res;
      try {
        res = m_channel->send(m);
      } catch (const std::exception& e) {
        res = {false, "", e.what()};
      }
      if (res.ok) {
        c.exec("UPDATE notifications SET status = 'SENT', sent_at = now(), attempts = attempts + 1, provider = $2 WHERE id = $1",
               Params(m.id, res.provider));
        ++sent;
      } else {
        c.exec("UPDATE notifications SET attempts = attempts + 1, last_error = $2, "
               "status = CASE WHEN attempts + 1 >= 3 THEN 'FAILED' ELSE 'PENDING' END, "
               "scheduled_for = now() + interval '5 minutes' WHERE id = $1",
               Params(m.id, res.error));
      }
    });
    return sent;
  });
}

}  // namespace cf
