// ClinicFlow API server — composition root.
// Wires configuration, database, services and controllers together (manual
// dependency injection keeps the object graph explicit and testable).

#include <atomic>
#include <csignal>
#include <thread>

#include "abdm/AbdmService.hpp"
#include "abdm/MockAbdmGateway.hpp"
#include "audit/AuditService.hpp"
#include "common/HttpPlumbing.hpp"
#include "config/Config.hpp"
#include "controller/AbdmController.hpp"
#include "controller/AdminController.hpp"
#include "controller/FacilityController.hpp"
#include "controller/DermController.hpp"
#include "controller/HandwritingController.hpp"
#include "hwr/HandwritingRecognizer.hpp"
#include "controller/AuthController.hpp"
#include "controller/BillingController.hpp"
#include "controller/ClinicalController.hpp"
#include "controller/PatientController.hpp"
#include "controller/SchedulingController.hpp"
#include "db/Db.hpp"
#include "db/Migrator.hpp"
#include "oatpp-swagger/Controller.hpp"
#include "oatpp/network/Server.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"
#include "security/AuthService.hpp"
#include "security/Crypto.hpp"
#include "service/AdminService.hpp"
#include "service/BillingService.hpp"
#include "service/ClinicalService.hpp"
#include "service/NotificationService.hpp"
#include "service/PatientService.hpp"
#include "service/SchedulingService.hpp"

namespace {
std::atomic<bool> g_running{true};
oatpp::network::Server* g_server = nullptr;
void onSignal(int) {
  g_running = false;
  if (g_server) g_server->stop();
}
}  // namespace

static std::shared_ptr<oatpp::swagger::DocumentInfo> swaggerInfo(const cf::Config& cfg) {
  oatpp::swagger::DocumentInfo::Builder builder;
  auto bearer = oatpp::swagger::SecurityScheme::createShared();
  bearer->type = "http";
  bearer->scheme = "bearer";
  bearer->description = "Session token from POST /api/v1/auth/login";
  builder.setTitle("ClinicFlow — Mini Clinic Management System API")
      .setDescription(
          "Proof-of-concept API. ABDM functionality runs against a MOCK adapter and does not perform real ABDM "
          "transactions. This software is not NABH- or ABDM-certified.")
      .setVersion("0.1.0")
      .addServer("/", "Current host")
      .addSecurityScheme("bearerAuth", bearer);
  return builder.build();
}

static void run() {
  auto cfg = cf::Config::fromEnv();
  OATPP_LOGI("ClinicFlow", "Starting (env=%s, abdm=%s)", cfg.appEnv.c_str(), cfg.abdmMode.c_str());

  if (cfg.runMigrations) cf::Migrator::run(cfg.migrationDatabaseUrl, cfg.migrationsDir);
  auto db = std::make_shared<cf::Db>(cfg.databaseUrl, cfg.dbPoolSize);
  auto cipher = std::make_shared<cf::crypto::FieldCipher>(cfg.dataEncryptionKeyB64, cfg.blindIndexKeyB64);

  // Services
  auto audit = std::make_shared<cf::AuditService>(db);
  auto auth = std::make_shared<cf::AuthService>(db, audit, cfg);
  auth->bootstrapAdmin(cfg);
  auto admin = std::make_shared<cf::AdminService>(db, audit);
  auto facilities = std::make_shared<cf::FacilityService>(db, audit);
  auto notifications = std::make_shared<cf::NotificationService>(db, std::make_shared<cf::LogNotificationChannel>());
  auto patients = std::make_shared<cf::PatientService>(db, audit, cipher);
  auto scheduling = std::make_shared<cf::SchedulingService>(db, audit, notifications);
  auto clinical = std::make_shared<cf::ClinicalService>(db, audit);
  auto derm = std::make_shared<cf::DermService>(db, audit, cipher);
  clinical->addExtension(derm);  // safety acknowledgement + case capture at finalize
  auto recognizer = cf::hwr::makeRecognizerFromEnv();
  OATPP_LOGI("ClinicFlow", "Handwriting recognition: %s", recognizer->description().c_str());
  auto handwriting = std::make_shared<cf::HandwritingService>(db, audit, cipher, recognizer);
  facilities->onNetworkLeave([derm](cf::Conn& c, const std::string& fid) { derm->withdrawFacilityFromNetwork(c, fid); });
  auto billing = std::make_shared<cf::BillingService>(db, audit);
  auto gateway = std::make_shared<cf::MockAbdmGateway>();
  auto abdm = std::make_shared<cf::AbdmService>(db, audit, cipher, gateway, clinical, patients);

  // JSON mapping
  auto ser = oatpp::parser::json::mapping::Serializer::Config::createShared();
  ser->includeNullFields = false;
  auto deser = oatpp::parser::json::mapping::Deserializer::Config::createShared();
  deser->allowUnknownFields = true;  // DTOs whitelist fields: unknown keys are ignored, never mass-assigned
  std::shared_ptr<oatpp::data::mapping::ObjectMapper> om =
      oatpp::parser::json::mapping::ObjectMapper::createShared(ser, deser);

  // Controllers
  auto router = oatpp::web::server::HttpRouter::createShared();
  oatpp::web::server::api::Endpoints docEndpoints;
  auto addController = [&](const std::shared_ptr<oatpp::web::server::api::ApiController>& c) {
    router->addController(c);
    docEndpoints.append(c->getEndpoints());
  };
  bool tp = cfg.trustProxyHeaders;
  addController(std::make_shared<cf::AuthController>(om, auth, tp));
  addController(std::make_shared<cf::AdminController>(om, auth, tp, admin, audit, db, cfg.abdmMode));
  addController(std::make_shared<cf::FacilityController>(om, auth, tp, facilities));
  addController(std::make_shared<cf::DermController>(om, auth, tp, derm));
  addController(std::make_shared<cf::HandwritingController>(om, auth, tp, handwriting));
  addController(std::make_shared<cf::PatientController>(om, auth, tp, patients, clinical, scheduling, billing));
  addController(std::make_shared<cf::SchedulingController>(om, auth, tp, scheduling));
  addController(std::make_shared<cf::ClinicalController>(om, auth, tp, clinical));
  addController(std::make_shared<cf::BillingController>(om, auth, tp, billing));
  addController(std::make_shared<cf::AbdmController>(om, auth, tp, abdm));

  if (!cfg.swaggerResPath.empty()) {
    auto resources = oatpp::swagger::Resources::loadResources(cfg.swaggerResPath.c_str());
    auto docInfo = swaggerInfo(cfg);
    router->addController(oatpp::swagger::Controller::createShared(docEndpoints, docInfo, resources));
    OATPP_LOGI("ClinicFlow", "Swagger UI at /swagger/ui, OpenAPI JSON at /api-docs/oas-3.0.0.json");
  }

  auto handler = oatpp::web::server::HttpConnectionHandler::createShared(router);
  handler->setErrorHandler(std::make_shared<cf::JsonErrorHandler>(om));
  handler->addRequestInterceptor(std::make_shared<cf::BodyCaptureInterceptor>(om));
  handler->addResponseInterceptor(std::make_shared<cf::SecurityHeadersInterceptor>());

  auto provider = oatpp::network::tcp::server::ConnectionProvider::createShared(
      {cfg.host, static_cast<v_uint16>(cfg.port), oatpp::network::Address::IP_4});
  oatpp::network::Server server(provider, handler);
  g_server = &server;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Background worker: reminders, consent expiry, ABDM retries.
  std::thread worker([&] {
    // Background jobs span all clinics (each row carries its own facility_id).
    cf::ScopedFacility workerScope("", true);
    int elapsed = cfg.workerIntervalSeconds;
    while (g_running) {
      if (elapsed >= cfg.workerIntervalSeconds) {
        elapsed = 0;
        try {
          notifications->dispatchDue();
          abdm->runMaintenance();
        } catch (const std::exception& e) {
          OATPP_LOGE("Worker", "%s", e.what());
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
      ++elapsed;
    }
  });

  OATPP_LOGI("ClinicFlow", "Listening on %s:%d", cfg.host.c_str(), cfg.port);
  server.run();
  g_running = false;
  worker.join();
  provider->stop();
  handler->stop();
}

int main() {
  oatpp::base::Environment::init();
  int rc = 0;
  try {
    run();
  } catch (const std::exception& e) {
    OATPP_LOGE("ClinicFlow", "Fatal: %s", e.what());
    rc = 1;
  }
  oatpp::base::Environment::destroy();
  return rc;
}
