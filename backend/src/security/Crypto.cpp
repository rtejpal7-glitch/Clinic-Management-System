#include "Crypto.hpp"

#include <argon2.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace cf::crypto {

std::string randomBytes(size_t n) {
  std::string out(n, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(n)) != 1) {
    throw std::runtime_error("CSPRNG failure");
  }
  return out;
}

std::string base64Encode(const std::string& in, bool urlSafe) {
  std::string out(4 * ((in.size() + 2) / 3) + 1, '\0');
  int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                          reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size()));
  out.resize(n);
  if (urlSafe) {
    for (auto& c : out) {
      if (c == '+') c = '-';
      else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
  }
  return out;
}

std::string base64Decode(const std::string& input) {
  std::string in = input;
  for (auto& c : in) {
    if (c == '-') c = '+';
    else if (c == '_') c = '/';
  }
  while (in.size() % 4) in += '=';
  std::string out(3 * in.size() / 4 + 1, '\0');
  int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                          reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size()));
  if (n < 0) throw std::runtime_error("Invalid base64");
  size_t pad = 0;
  if (!in.empty() && in.back() == '=') pad++;
  if (in.size() > 1 && in[in.size() - 2] == '=') pad++;
  out.resize(static_cast<size_t>(n) - pad);
  return out;
}

static std::string toHex(const unsigned char* d, size_t n) {
  static const char* hex = "0123456789abcdef";
  std::string s(n * 2, '0');
  for (size_t i = 0; i < n; ++i) {
    s[2 * i] = hex[d[i] >> 4];
    s[2 * i + 1] = hex[d[i] & 0xF];
  }
  return s;
}

std::string sha256Hex(const std::string& data) {
  unsigned char md[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), md);
  return toHex(md, SHA256_DIGEST_LENGTH);
}

std::string hmacSha256Hex(const std::string& key, const std::string& data) {
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), reinterpret_cast<const unsigned char*>(data.data()),
       data.size(), md, &len);
  return toHex(md, len);
}

std::string randomToken(size_t bytes) { return base64Encode(randomBytes(bytes), true); }

// ---------------- FieldCipher ----------------
FieldCipher::FieldCipher(const std::string& keyB64, const std::string& blindIndexKeyB64) {
  m_key = base64Decode(keyB64);
  m_indexKey = base64Decode(blindIndexKeyB64);
  if (m_key.size() != 32) throw std::runtime_error("DATA_ENCRYPTION_KEY must be 32 bytes (base64-encoded)");
  if (m_indexKey.size() < 32) throw std::runtime_error("BLIND_INDEX_KEY must be at least 32 bytes (base64-encoded)");
  if (m_key == m_indexKey) throw std::runtime_error("DATA_ENCRYPTION_KEY and BLIND_INDEX_KEY must differ");
}

using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

std::string FieldCipher::encrypt(const std::string& plaintext) const {
  std::string nonce = randomBytes(12);
  CtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  std::string ct(plaintext.size() + 16, '\0');
  int len = 0, total = 0;
  if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
      EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(m_key.data()),
                         reinterpret_cast<const unsigned char*>(nonce.data())) != 1 ||
      EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(ct.data()), &len,
                        reinterpret_cast<const unsigned char*>(plaintext.data()), static_cast<int>(plaintext.size())) != 1) {
    throw std::runtime_error("Encryption failed");
  }
  total = len;
  if (EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(ct.data()) + total, &len) != 1)
    throw std::runtime_error("Encryption failed");
  total += len;
  ct.resize(total);
  std::string tag(16, '\0');
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1)
    throw std::runtime_error("Encryption failed");
  return "v1:" + base64Encode(nonce + ct + tag);
}

std::string FieldCipher::decrypt(const std::string& token) const {
  if (token.rfind("v1:", 0) != 0) throw std::runtime_error("Unsupported ciphertext version");
  std::string raw = base64Decode(token.substr(3));
  if (raw.size() < 28) throw std::runtime_error("Ciphertext too short");
  std::string nonce = raw.substr(0, 12);
  std::string tag = raw.substr(raw.size() - 16);
  std::string ct = raw.substr(12, raw.size() - 28);
  CtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  std::string pt(ct.size() + 16, '\0');
  int len = 0, total = 0;
  if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
      EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, reinterpret_cast<const unsigned char*>(m_key.data()),
                         reinterpret_cast<const unsigned char*>(nonce.data())) != 1 ||
      EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(pt.data()), &len,
                        reinterpret_cast<const unsigned char*>(ct.data()), static_cast<int>(ct.size())) != 1) {
    throw std::runtime_error("Decryption failed");
  }
  total = len;
  EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, const_cast<char*>(tag.data()));
  if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(pt.data()) + total, &len) != 1) {
    throw std::runtime_error("Decryption failed: authentication tag mismatch");
  }
  total += len;
  pt.resize(total);
  return pt;
}

std::string FieldCipher::blindIndex(const std::string& normalizedValue) const {
  return hmacSha256Hex(m_indexKey, normalizedValue);
}

// ---------------- Passwords ----------------
namespace password {
static const uint32_t T_COST = 2;
static const uint32_t M_COST_KIB = 19456;
static const uint32_t PARALLELISM = 1;
static const size_t SALT_LEN = 16;
static const size_t HASH_LEN = 32;

std::string hash(const std::string& pw) {
  std::string salt = randomBytes(SALT_LEN);
  size_t encLen = argon2_encodedlen(T_COST, M_COST_KIB, PARALLELISM, SALT_LEN, HASH_LEN, Argon2_id);
  std::string encoded(encLen, '\0');
  int rc = argon2id_hash_encoded(T_COST, M_COST_KIB, PARALLELISM, pw.data(), pw.size(), salt.data(), salt.size(),
                                 HASH_LEN, encoded.data(), encLen);
  if (rc != ARGON2_OK) throw std::runtime_error(std::string("Password hashing failed: ") + argon2_error_message(rc));
  encoded.resize(strlen(encoded.c_str()));
  return encoded;
}

bool verify(const std::string& encodedHash, const std::string& pw) {
  if (encodedHash.rfind("$argon2id$", 0) != 0) return false;
  return argon2id_verify(encodedHash.c_str(), pw.data(), pw.size()) == ARGON2_OK;
}
}  // namespace password

}  // namespace cf::crypto
