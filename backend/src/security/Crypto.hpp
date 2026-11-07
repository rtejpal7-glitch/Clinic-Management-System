#pragma once
// Cryptographic primitives built on OpenSSL 3.
//  - CSPRNG tokens, SHA-256, HMAC-SHA256
//  - AES-256-GCM authenticated encryption for field-level encryption at rest
//  - Argon2id password hashing (reference implementation, libargon2)

#include <string>

namespace cf::crypto {

std::string randomBytes(size_t n);
std::string base64Encode(const std::string& in, bool urlSafe = false);
std::string base64Decode(const std::string& in);
std::string sha256Hex(const std::string& data);
std::string hmacSha256Hex(const std::string& key, const std::string& data);
std::string randomToken(size_t bytes = 32);  // URL-safe base64

// Field encryption. Output format: "v1:" + base64(nonce[12] | ciphertext | tag[16]).
// The version prefix allows key rotation (v2 key, re-encrypt in background).
class FieldCipher {
 public:
  FieldCipher(const std::string& keyB64, const std::string& blindIndexKeyB64);
  std::string encrypt(const std::string& plaintext) const;
  std::string decrypt(const std::string& token) const;
  // Deterministic keyed hash used to look up encrypted values by equality.
  std::string blindIndex(const std::string& normalizedValue) const;

 private:
  std::string m_key;
  std::string m_indexKey;
};

// Password hashing with Argon2id (OWASP recommended parameters: m=19 MiB, t=2, p=1).
namespace password {
std::string hash(const std::string& password);
bool verify(const std::string& encodedHash, const std::string& password);
}  // namespace password

}  // namespace cf::crypto
