#pragma once
// Handwritten consultation notes: ink storage (encrypted), recognition through a
// configurable provider, structured extraction, and doctor review/acceptance.

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "audit/AuditService.hpp"
#include "db/Db.hpp"
#include "hwr/HandwritingRecognizer.hpp"
#include "security/AuthContext.hpp"
#include "security/Crypto.hpp"

namespace cf {

class HandwritingService {
 public:
  using json = nlohmann::json;
  HandwritingService(std::shared_ptr<Db> db, std::shared_ptr<AuditService> audit, std::shared_ptr<crypto::FieldCipher> cipher,
                     std::shared_ptr<hwr::HandwritingRecognizer> recognizer)
      : m_db(std::move(db)), m_audit(std::move(audit)), m_cipher(std::move(cipher)), m_recognizer(std::move(recognizer)) {}

  json overview(const AuthContext& ctx, const std::string& consultationId) const;
  json pageStrokes(const AuthContext& ctx, const std::string& consultationId, int pageNo) const;
  std::string pageImage(const AuthContext& ctx, const std::string& consultationId, int pageNo) const;  // PNG bytes
  json savePage(const AuthContext& ctx, const std::string& consultationId, int pageNo, const json& in);
  json deletePage(const AuthContext& ctx, const std::string& consultationId, int pageNo);

  json analyse(const AuthContext& ctx, const std::string& consultationId, const json& in);
  json extract(const AuthContext& ctx, const std::string& consultationId, const json& in) const;
  json accept(const AuthContext& ctx, const std::string& transcriptionId, const json& in);
  json discard(const AuthContext& ctx, const std::string& transcriptionId, const json& in);

  json recognizerInfo() const;

 private:
  json extractFor(Conn& c, const std::string& text) const;
  json transcriptionJson(const Row& r) const;

  std::shared_ptr<Db> m_db;
  std::shared_ptr<AuditService> m_audit;
  std::shared_ptr<crypto::FieldCipher> m_cipher;
  std::shared_ptr<hwr::HandwritingRecognizer> m_recognizer;
};

}  // namespace cf
