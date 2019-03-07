// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_OPENSSL_ENGINE_H_
#define DEVTOOLS_GOMA_CLIENT_OPENSSL_ENGINE_H_

#ifdef _WIN32
#include "socket_helper_win.h"
#endif

#include <openssl/ssl.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "autolock_timer.h"
#include "tls_engine.h"

using std::string;

namespace devtools_goma {

class HttpRequest;
class HttpResponse;
class OneshotClosure;
class ScopedSocket;

class ScopedX509CRLFree {
 public:
  inline void operator()(X509_CRL *x) const { if (x) X509_CRL_free(x); }
};

typedef std::unique_ptr<X509_CRL, ScopedX509CRLFree> ScopedX509CRL;

class OpenSSLContext {
 public:
  OpenSSLContext();
  ~OpenSSLContext();

  // |invalidate_closure| should be one-shot closure.
  // It will be deleted after running.
  // |invalidate_closure| MUST NOT call any OpenSSLContext methods
  // to avoid dead lock.
  void Init(const string& hostname,
            absl::optional<absl::Duration> crl_max_valid_duration,
            OneshotClosure* invalidate_closure);
  // Set proxy to be used to download CRLs.
  void SetProxy(const string& proxy_host, const int proxy_port);

  // Returns true if server's identity is valid.
  bool IsValidServerIdentity(X509* cert);

  // Returns true if one of X509 certificates have revoked.
  bool IsRevoked(STACK_OF(X509)* x509s);

  string GetCertsInfo() const {
    AUTOLOCK(lock, &mu_);
    return certs_info_;
  }
  bool IsCrlReady() const {
    AUTOLOCK(lock, &mu_);
    return is_crl_ready_;
  }
  const string GetLastErrorMessage() const {
    AUTOLOCK(lock, &mu_);
    return last_error_;
  }
  size_t ref_cnt() { return ref_cnt_; }
  SSL* NewSSL();
  void DeleteSSL(SSL* ssl);
  void RecordSession(SSL* ssl);
  void Invalidate();

  // Returns true if |hostname| matches |pattern|.
  // |pattern| may have a wildcard explained in RFC2818 Section 3.1.
  // See: http://tools.ietf.org/html/rfc2818#section-3.1
  //
  // Limitation: it does not support the case with multiple wildcards.
  static bool IsHostnameMatched(absl::string_view hostname,
                                absl::string_view pattern);

  const string& hostname() { return hostname_; }

 private:
  ScopedX509CRL GetX509CrlsFromUrl(const string& url, string* crl_str);

  // Loads CRLs based on X509v3 CRL distribution point.
  bool SetupCrlsUnlocked(STACK_OF(X509) * x509s) EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Lock for OpenSSL context (ctx_, crls_, is_crl_ready_, certs_info_
  // and last_error_).
  mutable Lock mu_;
  SSL_CTX* ctx_;
  // Since we do not know good way to get CRLs from SSL_CTX, we will use crls_
  // to check revoked certificate.
  std::vector<ScopedX509CRL> crls_;
  string proxy_host_;
  int proxy_port_;
  string certs_info_;
  string hostname_;
  bool is_crl_ready_;
  string last_error_;
  absl::optional<absl::Time> last_error_time_;
  absl::optional<absl::Duration> crl_max_valid_duration_;

  // ref_cnt_ represents the number of OpenSSLEngine using the class instance.
  // It is increased by NewSSL, and decreased by DeleteSSL.
  // If ref_cnt_ become 0, OpenSSLEngineCache can delete the instance.
  //
  // Note: NewSSL, DeleteSSL, ref_cnt() MUST be called under
  // OpenSSLEngineCache lock. Or, OpenSSLEngineCache may delete OpenSSLContext
  // in use:
  // e.g. th1: checks ref_cnt() == 0 -> th2: NewTLSEngine -> th1: delete.
  size_t ref_cnt_;

  OneshotClosure* notify_invalidate_closure_;

  DISALLOW_COPY_AND_ASSIGN(OpenSSLContext);
};

// OpenSSLEngine is not synchronized.
class OpenSSLEngine : public TLSEngine {
 public:
  bool IsIOPending() const override;
  bool IsReady() const override;

  int GetDataToSendTransport(string* data) override;
  size_t GetBufSizeFromTransport() override;
  int SetDataFromTransport(const absl::string_view& data) override;

  int Read(void* data, int size) override;
  int Write(const void* data, int size) override;

  string GetLastErrorMessage() const override;

  // Shows this engine has already used before.
  bool IsRecycled() const override { return recycled_; }

 protected:
  friend class OpenSSLEngineCache;
  OpenSSLEngine();
  ~OpenSSLEngine() override;
  // Will not take ownership of ctx.
  void Init(OpenSSLContext* ctx);
  void SetRecycled() { recycled_ = true; }

 private:
  friend std::unique_ptr<OpenSSLEngine>::deleter_type;

  // Returns |return_value| if |return_value| > 0.
  // Note that positive |return_value| usually means the number of data
  // read / written.
  // Otherwise, returns TLSEngine::TLSErrorReason to make a caller know
  // error reason.
  int UpdateStatus(int return_value);

  // Returns 1 if TLS handshake was successfully completed, and a TLS connection
  // has been established.
  // Otherwise, returns TLSEngine::TLSErrorReason to make a caller know
  // error reason.
  int Connect();
  string GetErrorString() const;

  SSL* ssl_;
  BIO* network_bio_;
  bool want_read_;
  bool want_write_;
  bool recycled_;
  bool need_self_verify_;
  OpenSSLContext* ctx_;  // OpenSSLEngineCache has ownership.

  enum SSL_ENGINE_STATE { BEFORE_INIT, IN_CONNECT, READY } state_;

  DISALLOW_COPY_AND_ASSIGN(OpenSSLEngine);
};

class OpenSSLEngineCache : public TLSEngineFactory {
 public:
  OpenSSLEngineCache();
  ~OpenSSLEngineCache() override;
  TLSEngine* NewTLSEngine(int sock) override;
  void WillCloseSocket(int sock) override;
  void AddCertificateFromFile(const string& ssl_cert_filename);
  void AddCertificateFromString(const string& ssl_cert);
  string GetCertsInfo() override {
    return ctx_->GetCertsInfo();
  }
  void SetHostname(const string& hostname) override {
    AUTOLOCK(lock, &mu_);
    hostname_ = hostname;
  }
  void SetProxy(const string& proxy_host, const int proxy_port) {
    AUTOLOCK(lock, &mu_);
    proxy_host_ = proxy_host;
    proxy_port_ = proxy_port;
  }
  void SetCRLMaxValidDuration(absl::optional<absl::Duration> duration) {
    crl_max_valid_duration_ = std::move(duration);
  }

 private:
  // This function should be called with mu_ lock held.
  std::unique_ptr<OpenSSLEngine> GetOpenSSLEngineUnlocked();
  void InvalidateContext();

  // Lock for ctx_, contexts_to_delete_, ssl_map_, certs_ and proxy configs.
  mutable Lock mu_;
  std::unique_ptr<OpenSSLContext> ctx_;
  std::vector<std::unique_ptr<OpenSSLContext>> contexts_to_delete_;
  std::unordered_map<int, std::unique_ptr<OpenSSLEngine>> ssl_map_;
  // Proxy configs to download CRLs.
  string hostname_;
  string proxy_host_;
  int proxy_port_;
  absl::optional<absl::Duration> crl_max_valid_duration_;

  DISALLOW_COPY_AND_ASSIGN(OpenSSLEngineCache);
};

}  // namespace devtools_goma
#endif  // DEVTOOLS_GOMA_CLIENT_OPENSSL_ENGINE_H_
