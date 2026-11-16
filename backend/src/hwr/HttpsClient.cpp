#include "HttpsClient.hpp"

#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <memory>
#include <regex>
#include <stdexcept>

namespace cf::hwr {

namespace {

struct Url {
  bool tls = true;
  std::string host, port, path;
};

Url parseUrl(const std::string& url) {
  static const std::regex re(R"(^(https?)://([^/:]+)(?::(\d+))?(/.*)?$)", std::regex::icase);
  std::smatch m;
  if (!std::regex_match(url, m, re)) throw std::runtime_error("invalid URL");
  Url u;
  std::string scheme = m.str(1);
  for (auto& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  u.tls = scheme == "https";
  u.host = m.str(2);
  u.port = m[3].matched ? m.str(3) : (u.tls ? "443" : "80");
  u.path = m[4].matched ? m.str(4) : "/";
  return u;
}

int connectTcp(const Url& u, int timeout) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res) != 0 || !res) throw std::runtime_error("cannot resolve " + u.host);
  int fd = -1;
  for (auto* p = res; p; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv{timeout, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) throw std::runtime_error("cannot connect to " + u.host + ":" + u.port);
  return fd;
}

std::string sslError() {
  unsigned long e = ERR_get_error();
  char buf[256];
  ERR_error_string_n(e, buf, sizeof buf);
  return buf;
}

}  // namespace

std::string decodeChunked(const std::string& body) {
  std::string out;
  size_t p = 0;
  while (p < body.size()) {
    size_t eol = body.find("\r\n", p);
    if (eol == std::string::npos) break;
    size_t len = std::stoul(body.substr(p, eol - p), nullptr, 16);
    if (len == 0) break;
    p = eol + 2;
    if (p + len > body.size()) throw std::runtime_error("truncated chunked response");
    out.append(body, p, len);
    p += len + 2;
  }
  return out;
}

HttpResponse httpPost(const std::string& url, const std::map<std::string, std::string>& headers, const std::string& body,
                      const HttpOptions& opts) {
  Url u = parseUrl(url);
  int fd = connectTcp(u, opts.timeoutSeconds);
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(nullptr, SSL_CTX_free);
  std::unique_ptr<SSL, decltype(&SSL_free)> ssl(nullptr, SSL_free);
  auto closeFd = std::unique_ptr<int, void (*)(int*)>(&fd, [](int* f) { if (*f >= 0) close(*f); });

  if (u.tls) {
    ctx.reset(SSL_CTX_new(TLS_client_method()));
    if (!ctx) throw std::runtime_error("TLS init failed");
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
    if (!opts.caFile.empty()) {
      if (SSL_CTX_load_verify_locations(ctx.get(), opts.caFile.c_str(), nullptr) != 1) throw std::runtime_error("cannot load CA file");
    } else {
      SSL_CTX_set_default_verify_paths(ctx.get());
    }
    ssl.reset(SSL_new(ctx.get()));
    SSL_set_tlsext_host_name(ssl.get(), u.host.c_str());
    SSL_set1_host(ssl.get(), u.host.c_str());  // hostname verification
    SSL_set_fd(ssl.get(), fd);
    if (SSL_connect(ssl.get()) != 1) throw std::runtime_error("TLS handshake with " + u.host + " failed: " + sslError());
  }
  auto writeAll = [&](const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
      int n = u.tls ? SSL_write(ssl.get(), s.data() + off, static_cast<int>(s.size() - off))
                    : static_cast<int>(::send(fd, s.data() + off, s.size() - off, 0));
      if (n <= 0) throw std::runtime_error("write failed");
      off += static_cast<size_t>(n);
    }
  };
  std::string req = "POST " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n";
  for (auto& [k, v] : headers) req += k + ": " + v + "\r\n";
  req += "\r\n";
  writeAll(req);
  writeAll(body);

  std::string raw;
  char buf[16384];
  for (;;) {
    int n = u.tls ? SSL_read(ssl.get(), buf, sizeof buf) : static_cast<int>(::recv(fd, buf, sizeof buf, 0));
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > opts.maxResponseBytes) throw std::runtime_error("response too large");
  }
  size_t hdrEnd = raw.find("\r\n\r\n");
  if (hdrEnd == std::string::npos) throw std::runtime_error("no HTTP response (timeout or connection closed)");
  HttpResponse r;
  std::string head = raw.substr(0, hdrEnd);
  std::smatch m;
  static const std::regex statusRe(R"(^HTTP/\d(?:\.\d)?\s+(\d{3}))");
  if (!std::regex_search(head, m, statusRe)) throw std::runtime_error("malformed HTTP status line");
  r.status = std::stoi(m.str(1));
  size_t p = head.find("\r\n");
  while (p != std::string::npos && p < head.size()) {
    size_t q = head.find("\r\n", p + 2);
    std::string line = head.substr(p + 2, (q == std::string::npos ? head.size() : q) - p - 2);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
      std::string k = line.substr(0, colon), v = line.substr(colon + 1);
      for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      v.erase(0, v.find_first_not_of(' '));
      r.headers[k] = v;
    }
    p = q;
  }
  r.body = raw.substr(hdrEnd + 4);
  auto te = r.headers.find("transfer-encoding");
  if (te != r.headers.end() && te->second.find("chunked") != std::string::npos) r.body = decodeChunked(r.body);
  return r;
}

}  // namespace cf::hwr
