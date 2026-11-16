#pragma once
// Minimal blocking HTTP/1.1 client over OpenSSL (TLS with certificate and
// hostname verification) or plain TCP for http:// URLs. Used only for outbound
// calls to a configured handwriting-recognition service.

#include <map>
#include <string>

namespace cf::hwr {

struct HttpResponse {
  int status = 0;
  std::map<std::string, std::string> headers;  // lower-case names
  std::string body;
};

struct HttpOptions {
  int timeoutSeconds = 30;
  std::string caFile;            // extra CA bundle (e.g. for a self-hosted service); system store otherwise
  size_t maxResponseBytes = 8 * 1024 * 1024;
};

// Throws std::runtime_error on network/TLS failures.
HttpResponse httpPost(const std::string& url, const std::map<std::string, std::string>& headers, const std::string& body,
                      const HttpOptions& opts = {});

// Exposed for tests
std::string decodeChunked(const std::string& body);

}  // namespace cf::hwr
