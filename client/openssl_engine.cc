// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "openssl_engine.h"

#include <openssl/asn1.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "file_dir.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "google/protobuf/io/zero_copy_stream.h"
MSVC_POP_WARNING()
#include "http.h"
#include "http_util.h"
#include "mypath.h"
#include "openssl_engine_helper.h"
#include "path.h"
#include "platform_thread.h"
#include "scoped_fd.h"
#include "socket_pool.h"

#ifndef OPENSSL_IS_BORINGSSL
#error "This code is written for BoringSSL"
#endif

namespace devtools_goma {

namespace {

// Prevent use of SSL on error for this period.
constexpr absl::Duration kErrorTimeout = absl::Seconds(60);

// Wait for this period if no more sockets are in the pool.
constexpr absl::Duration kWaitForThingsGetsBetter = absl::Seconds(1);

absl::once_flag g_openssl_init_once;

class ScopedBIOFree {
 public:
  inline void operator()(BIO *x) const { if (x) CHECK(BIO_free(x)); }
};

class ScopedX509Free {
 public:
  inline void operator()(X509 *x) const { if (x) X509_free(x); }
};

class ScopedX509StoreCtxFree {
 public:
  inline void operator()(X509_STORE_CTX *x) const {
    if (x) X509_STORE_CTX_free(x);
  }
};

class ScopedX509StoreFree {
 public:
  inline void operator()(X509_STORE *x) const { if (x) X509_STORE_free(x); }
};

template<typename T>
string GetHumanReadableInfo(T* data, int (*func)(BIO*, T*)) {
  std::unique_ptr<BIO, ScopedBIOFree> bio(BIO_new(BIO_s_mem()));
  func(bio.get(), data);
  char* x509_for_print;
  const int x509_for_print_len = BIO_get_mem_data(bio.get(), &x509_for_print);
  string ret(x509_for_print, x509_for_print_len);

  return ret;
}

string GetHumanReadableCert(X509* x509) {
  return GetHumanReadableInfo<X509>(x509, X509_print);
}

string GetHumanReadableCRL(X509_CRL* x509_crl) {
  return GetHumanReadableInfo<X509_CRL>(x509_crl, X509_CRL_print);
}

string GetHumanReadableCerts(STACK_OF(X509)* x509s) {
  string ret;
  for (size_t i = 0; i < sk_X509_num(x509s); i++) {
    ret.append(GetHumanReadableCert(sk_X509_value(x509s, i)));
  }
  return ret;
}

string GetHumanReadableSessionInfo(const SSL_SESSION* s) {
  std::ostringstream ss;
  ss << "SSL Session info:";
  ss << " protocol=" << SSL_SESSION_get_version(s);
  unsigned int len;
  const uint8_t* c = SSL_SESSION_get_id(s, &len);
  std::ostringstream sess_id;
  for (size_t i = 0; i < len; ++i) {
    sess_id << std::setfill('0') << std::setw(2)
            << std::hex << static_cast<int>(c[i]);
  }
  ss << " session_id=" << sess_id.str();
  ss << " time=" << SSL_SESSION_get_time(s);
  ss << " timeout=" << SSL_SESSION_get_timeout(s);
  return ss.str();
}

string GetHumanReadableSSLInfo(const SSL* ssl) {
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl);
  std::ostringstream ss;
  ss << "SSL info:";
  ss << " cipher:"
     << " name=" << SSL_CIPHER_get_name(cipher)
     << " bits=" << SSL_CIPHER_get_bits(cipher, nullptr)
     << " version=" << SSL_CIPHER_get_version(cipher);
  uint16_t curve_id = SSL_get_curve_id(ssl);
  if (curve_id != 0) {
    ss << " curve=" << SSL_get_curve_name(curve_id);
  }
  return ss.str();
}

// A class that controls lifetime of the SSL session.
class OpenSSLSessionCache {
 public:
  static void Init() {
    InitOpenSSLSessionCache();
  }

  // Set configs for the SSL session to the SSL context.
  static void Setup(SSL_CTX* ctx) {
    if (!cache_)
      InitOpenSSLSessionCache();

    DCHECK(cache_);
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
    SSL_CTX_sess_set_remove_cb(ctx, RemoveSessionCallBack);
  }

  // Set a session to a SSL structure instance if we have a cache.
  static bool SetCachedSession(SSL_CTX* ctx, SSL* ssl) {
    DCHECK(cache_);
    return cache_->SetCachedSessionInternal(ctx, ssl);
  }

  static void RecordSession(SSL* ssl) {
    DCHECK(cache_);
    DCHECK(ssl);
    SSL_SESSION* sess = SSL_get1_session(ssl);
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
    LOG(INFO) << "Storing SSL session."
              << " ctx=" << ctx
              << " session_info=" << GetHumanReadableSessionInfo(sess)
              << " secure_renegotiation_support="
              << SSL_get_secure_renegotiation_support(ssl);
    if (!cache_->RecordSessionInternal(ctx, sess)) {
      LOG(INFO) << "Tried to store already stored session.";
      // Since SSL_get1_session increases a reference count of |sess|,
      // we need to decrease the reference count here.
      // Note that we do not decrease the reference count if
      // RecordSessionInternal returned true because we need to keep
      // the session valid while we have it in our session cache store.
      SSL_SESSION_free(sess);
    }
  }

 private:
  OpenSSLSessionCache() {}
  ~OpenSSLSessionCache() {
    // Destructor deletes all cached sessions.
    for (auto& it : session_map_) {
      SSL_SESSION_free(it.second);
    }
    session_map_.clear();
  }

  static void InitOpenSSLSessionCache() {
    cache_ = new OpenSSLSessionCache();
    atexit(FinalizeOpenSSLSessionCache);
  }

  static void FinalizeOpenSSLSessionCache() {
    if (cache_)
      delete cache_;
    cache_ = nullptr;
  }

  static void RemoveSessionCallBack(SSL_CTX* ctx, SSL_SESSION* sess) {
    DCHECK(cache_);

    LOG(INFO) << "Released stored SSL session."
              << " session_info=" << GetHumanReadableSessionInfo(sess);
    cache_->RemoveSessionInternal(ctx);
  }

  // To avoid race condition, you SHOULD call SSL_set_session while
  // |mu_| is held.  Or, you may cause use-after-free.
  //
  // The SSL_SESSION instance life time is controlled by reference counting.
  // SSL_set_session increase the reference count, and SSL_SESSION_free
  // or SSL_free SSL instance that has the session decrease the reference
  // count.  When session is revoked, SSL_SESSION instance is free'd via
  // RemoveSession.  At the same time, RemoveSession removes the instance
  // from internal session_map_.
  // If you do SSL_set_session outside of |mu_| lock, you may use the
  // SSL_SESSION instance already free'd.
  // Note that increasing reference count and decreasing reference count
  // are done under a lock held by BoringSSL, we do not need to lock for them.
  // That is why we use ReadWriteLock.
  // TODO: use mutex lock if it is much faster than shared lock.
  bool SetCachedSessionInternal(SSL_CTX* ctx, SSL* ssl) {
    AUTO_SHARED_LOCK(lock, &mu_);
    SSL_SESSION* sess = GetInternalUnlocked(ctx);
    if (sess == nullptr)
      return false;

    VLOG(3) << "Reused session."
            << " ctx=" << ctx
            << " session_info=" << GetHumanReadableSessionInfo(sess);
    SSL_set_session(ssl, sess);
    return true;
  }

  // Returns true if the session is added.
  bool RecordSessionInternal(SSL_CTX* ctx, SSL_SESSION* session) {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    if (GetInternalUnlocked(ctx) != nullptr)
      return false;

    CHECK(session_map_.insert(std::make_pair(ctx, session)).second);
    return true;
  }

  // Returns true if the session is removed.
  bool RemoveSessionInternal(SSL_CTX* ctx) {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    auto found = session_map_.find(ctx);
    if (found == session_map_.end()) {
      return false;
    }

    // Decrement reference count to revoke the session when nobody use it.
    // See: https://www.openssl.org/docs/ssl/SSL_SESSION_free.html
    SSL_SESSION_free(found->second);
    session_map_.erase(found);
    return true;
  }

  SSL_SESSION* GetInternalUnlocked(SSL_CTX* ctx) {
    auto found = session_map_.find(ctx);
    if (found != session_map_.end()) {
      return found->second;
    }
    return nullptr;
  }

  mutable ReadWriteLock mu_;
  // Won't take ownership of SSL_CTX*.
  // Ownership of SSL_SESSION* is kept by the OpenSSL library, but
  // we decrement a reference count to notify it an obsolete session.
  std::unordered_map<SSL_CTX*, SSL_SESSION*> session_map_;

  static OpenSSLSessionCache* cache_;

  DISALLOW_COPY_AND_ASSIGN(OpenSSLSessionCache);
};

/* static */
OpenSSLSessionCache* OpenSSLSessionCache::cache_ = nullptr;

// A class that controls socket_pool used in OpenSSL engine.
class OpenSSLSocketPoolCache {
 public:
  static void Init() {
    if (!cache_) {
      cache_.reset(new OpenSSLSocketPoolCache);
      atexit(FinalizeOpenSSLSocketPoolCache);
    }
  }

  static SocketPool* GetSocketPool(const string& host, int port) {
    DCHECK(cache_);
    return cache_->GetSocketPoolInternal(host, port);
  }

 private:
  OpenSSLSocketPoolCache() {}
  ~OpenSSLSocketPoolCache() = default;
  friend std::unique_ptr<OpenSSLSocketPoolCache>::deleter_type;

  static void FinalizeOpenSSLSocketPoolCache() { cache_.reset(); }

  SocketPool* GetSocketPoolInternal(const string& host, int port) {
    std::ostringstream ss;
    ss << host << ":" << port;
    const string key = ss.str();

    AUTOLOCK(lock, &socket_pool_mu_);
    auto p = socket_pools_.emplace(key, nullptr);
    if (p.second) {
      p.first->second = absl::make_unique<SocketPool>(host, port);
    }
    return p.first->second.get();
  }

  Lock socket_pool_mu_;
  std::unordered_map<string, std::unique_ptr<SocketPool>> socket_pools_;

  static std::unique_ptr<OpenSSLSocketPoolCache> cache_;
  DISALLOW_COPY_AND_ASSIGN(OpenSSLSocketPoolCache);
};

/* static */
std::unique_ptr<OpenSSLSocketPoolCache> OpenSSLSocketPoolCache::cache_ =
    nullptr;

class OpenSSLCertificateStore {
 public:
  static void Init() {
    if (!store_) {
      store_ = new OpenSSLCertificateStore;
      store_->InitInternal();
      atexit(FinalizeOpenSSLCertificateStore);
    }
  }

  static bool AddCertificateFromFile(const string& filename) {
    DCHECK(store_);
    if (store_->IsKnownCertfileInternal(filename)) {
      LOG(INFO) << "Known cerficiate:" << filename;
      return false;
    }

    string user_cert;
    if (!ReadFileToString(filename.c_str(), &user_cert)) {
      LOG(ERROR) << "Failed to read:" << filename;
      return false;
    }
    return store_->AddCertificateFromStringInternal(filename, user_cert);
  }

  static bool AddCertificateFromString(
      const string& source, const string& cert) {
    DCHECK(store_);
    return store_->AddCertificateFromStringInternal(source, cert);
  }

  static void SetCertsToCTX(SSL_CTX* ctx) {
    DCHECK(store_);
    store_->SetCertsToCTXInternal(ctx);
  }

  static bool IsReady() {
    DCHECK(store_);
    return store_->IsReadyInternal();
  }

  static string GetTrustedCertificates() {
    DCHECK(store_);
    return store_->GetTrustedCertificatesInternal();
  }

 private:
  OpenSSLCertificateStore() {}
  ~OpenSSLCertificateStore() {}

  static void FinalizeOpenSSLCertificateStore() {
    delete store_;
    store_ = nullptr;
  }

  void InitInternal() {
    string root_certs;
    CHECK(GetTrustedRootCerts(&root_certs))
        << "Failed to read trusted root certificates from the system.";
    AddCertificateFromStringInternal("system", root_certs);
    LOG(INFO) << "Loaded root certificates.";
  }

  bool IsReadyInternal() const {
    AUTO_SHARED_LOCK(lock, &mu_);
    return certs_.size() != 0;
  }

  // Note: you must not return the value via const reference.
  // trusted_certificates_ is a member of the class, which is protected
  // by the mutex (mu_).  It could be updated after return of the function
  // by another thread.
  string GetTrustedCertificatesInternal() const {
    AUTO_SHARED_LOCK(lock, &mu_);
    return trusted_certificates_;
  }

  void SetCertsToCTXInternal(SSL_CTX* ctx) const {
    AUTO_SHARED_LOCK(lock, &mu_);
    for (const auto& it : certs_) {
      LOG(INFO) << "setting certs from: " << it.first
                << " size=" << it.second->size();
      for (const auto& x509 : *it.second) {
        X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), x509.get());
      }
    }
  }

  bool IsKnownCertfileInternal(const string& filename) const {
    AUTO_SHARED_LOCK(lock, &mu_);
    return certs_.find(filename) != certs_.end();
  }

  bool AddCertificateFromStringInternal(const string& source,
                                        const string& cert) {
    // Create BIO instance to be used by PEM_read_bio_X509_AUX.
    std::unique_ptr<BIO, ScopedBIOFree> bio(
        BIO_new_mem_buf(cert.data(), cert.size()));

    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    auto it = certs_.insert(std::make_pair(source, nullptr));
    if (!it.second) {
      LOG(WARNING) << "cert store already has certificate for "
                   << source;
      return false;
    }
    it.first->second =
        absl::make_unique<std::vector<std::unique_ptr<X509, ScopedX509Free>>>();
    for (;;) {
      std::unique_ptr<X509, ScopedX509Free> x509(
          PEM_read_bio_X509_AUX(bio.get(), nullptr, nullptr, nullptr));
      if (x509.get() == nullptr)
        break;

      const string readable_cert = GetHumanReadableCert(x509.get());
      LOG(INFO) << "Certificate loaded from " << source << ": "
                << readable_cert;
      trusted_certificates_.append(readable_cert);
      it.first->second->emplace_back(std::move(x509));
    }
    if (ERR_GET_REASON(ERR_peek_last_error()) == PEM_R_NO_START_LINE)
      ERR_clear_error();
    else
      LOG(ERROR) << "Unexpected error occured during reading SSL certificate."
                 << " source:" << source;
    // TODO: log error with source info when no certificate found.
    LOG_IF(ERROR, it.first->second->size() == 0)
        << "No certificate found in " << source;
    return it.first->second->size() > 0;
  }

  mutable ReadWriteLock mu_;
  std::map<string,
           std::unique_ptr<
               std::vector<std::unique_ptr<X509, ScopedX509Free>>>> certs_;

  string trusted_certificates_;

  static OpenSSLCertificateStore* store_;
  DISALLOW_COPY_AND_ASSIGN(OpenSSLCertificateStore);
};

/* static */
OpenSSLCertificateStore* OpenSSLCertificateStore::store_ = nullptr;

class OpenSSLCRLCache {
 public:
  static void Init() {
    if (!cache_) {
      cache_ = new OpenSSLCRLCache;
      atexit(FinalizeOpenSSLCRLCache);
    }
  }

  // Caller owns returned X509_CRL*.
  // It is caller's responsibility to free it with X509_CRL_free.
  static ScopedX509CRL LookupCRL(const string& url) {
    DCHECK(cache_);
    return cache_->LookupCRLInternal(url);
  }

  // Returns true if url exists in internal database and successfully removed.
  // Otherwise, e.g. not registered, returns false.
  static bool DeleteCRL(const string& url) {
    DCHECK(cache_);
    return cache_->DeleteCRLInternal(url);
  }

  // Won't take ownership of |crl|.  This function duplicates it internally.
  static void SetCRL(const string& url, X509_CRL* crl) {
    DCHECK(cache_);
    return cache_->SetCRLInternal(url, crl);
  }

 private:
  OpenSSLCRLCache() {}
  ~OpenSSLCRLCache() {
    crls_.clear();
  }
  static void FinalizeOpenSSLCRLCache() {
    delete cache_;
    cache_ = nullptr;
  }

  // Note: caller should free X509_CRL.
  ScopedX509CRL LookupCRLInternal(const string& url) {
    AUTO_SHARED_LOCK(lock, &mu_);
    const auto& it = crls_.find(url);
    if (it == crls_.end())
      return nullptr;
    return ScopedX509CRL(X509_CRL_dup(it->second.get()));
  }

  bool DeleteCRLInternal(const string& url) {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    const auto& it = crls_.find(url);
    if (it == crls_.end())
      return false;
    crls_.erase(it);
    return true;
  }

  void SetCRLInternal(const string& url, X509_CRL* crl) {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    if (crls_.count(url) > 0) {
      DeleteCRLInternal(url);
    }
    CHECK(crls_.insert(
            std::make_pair(url, ScopedX509CRL(X509_CRL_dup(crl)))).second)
        << "We already have the same URL in CRL store."
        << " url=" << url;
  }

  mutable ReadWriteLock mu_;
  std::map<string, ScopedX509CRL> crls_;

  static OpenSSLCRLCache* cache_;
  DISALLOW_COPY_AND_ASSIGN(OpenSSLCRLCache);
};

/* static */
OpenSSLCRLCache* OpenSSLCRLCache::cache_ = nullptr;

// Goma client also uses BoringSSL.
// Let's follow chromium's net/socket/ssl_client_socket_impl.cc.
// It uses BoringSSL default but avoid to select CBC ciphers.
const char* kCipherList = "ALL:!SHA256:!SHA384:!aPSK:!ECDSA+SHA1";
const int kCrlIoTimeout = 1000;  // milliseconds.
const size_t kMaxDownloadCrlRetry = 5;  // times.

void InitOpenSSL() {
  CRYPTO_library_init();
  OpenSSLSessionCache::Init();
  OpenSSLSocketPoolCache::Init();
  OpenSSLCertificateStore::Init();
  OpenSSLCRLCache::Init();
  LOG(INFO) << "OpenSSL is initialized.";
}

int NormalizeChar(int input) {
  if (!isalnum(input)) {
    return '_';
  }
  return input;
}

// Converts non-alphanum in a filename to '_'.
string NormalizeToUseFilename(const string& input) {
  string out(input);
  std::transform(out.begin(), out.end(), out.begin(), NormalizeChar);
  return out;
}

ScopedX509CRL ParseCrl(const string& crl_str) {
  // See: http://www.openssl.org/docs/apps/crl.html
  if (crl_str.find("-----BEGIN X509 CRL-----") != string::npos) {  // PEM
    std::unique_ptr<BIO, ScopedBIOFree> bio(
        BIO_new_mem_buf(crl_str.data(), crl_str.size()));
    return ScopedX509CRL(
        PEM_read_bio_X509_CRL(bio.get(), nullptr, nullptr, nullptr));
  }
  // DER
  const unsigned char* p =
      reinterpret_cast<const unsigned char*>(crl_str.data());
  return ScopedX509CRL(d2i_X509_CRL(nullptr, &p, crl_str.size()));
}

string GetSubjectCommonName(X509* x509) {
  static const size_t kMaxHostname = 1024;

  X509_NAME* subject = X509_get_subject_name(x509);
  char buf[kMaxHostname];
  if (X509_NAME_get_text_by_NID(subject, NID_commonName, buf, sizeof(buf))
      != -1) {
    return buf;
  }
  return "";
}

std::vector<string> GetAltDNSNames(X509* x509) {
  int index = X509_get_ext_by_NID(x509, NID_subject_alt_name, -1);
  if (index < 0) {
    LOG(INFO) << "cert has no subject alt name";
    return std::vector<string>();
  }
  X509_EXTENSION* subject_alt_name_extension = X509_get_ext(x509, index);
  if (!subject_alt_name_extension) {
    LOG(INFO) << "cert has no subject alt name extension";
    return std::vector<string>();
  }

  GENERAL_NAMES* subject_alt_names = reinterpret_cast<GENERAL_NAMES*>(
      X509V3_EXT_d2i(subject_alt_name_extension));
  if (!subject_alt_names) {
    LOG(INFO) << "unable to get subject alt name extension";
    return std::vector<string>();
  }
  VLOG(1) << "subject alt names=" << sk_GENERAL_NAME_num(subject_alt_names);

  std::vector<string> names;
  for (size_t i = 0; i < sk_GENERAL_NAME_num(subject_alt_names); ++i) {
    GENERAL_NAME* subject_alt_name =
        sk_GENERAL_NAME_value(subject_alt_names, i);
    switch (subject_alt_name->type) {
      case GEN_DNS:
        {
          unsigned char* dns_name =
              ASN1_STRING_data(subject_alt_name->d.dNSName);
          if (!dns_name)
            continue;
          int len = ASN1_STRING_length(subject_alt_name->d.dNSName);
          string name = string(reinterpret_cast<char*>(dns_name), len);
          VLOG(1) << "subject alt name[" << i << "]=" << name;
          names.push_back(name);
        }
        break;

      case GEN_IPADD:
        VLOG(1) << "ignore ip address";
        break;

      default:
        LOG(INFO) << "unsupported alt name type:" << subject_alt_name->type;
        break;
    }
  }
  sk_GENERAL_NAME_pop_free(subject_alt_names, GENERAL_NAME_free);
  return names;
}

bool MatchAltIPAddress(X509* x509, int af, void* ap) {
  int index = X509_get_ext_by_NID(x509, NID_subject_alt_name, -1);
  if (index < 0) {
    LOG(INFO) << "cert has no subject alt name";
    return false;
  }
  X509_EXTENSION* subject_alt_name_extension = X509_get_ext(x509, index);
  if (!subject_alt_name_extension) {
    LOG(INFO) << "cert has no subject alt name extension";
    return false;
  }

  GENERAL_NAMES* subject_alt_names = reinterpret_cast<GENERAL_NAMES*>(
      X509V3_EXT_d2i(subject_alt_name_extension));
  if (!subject_alt_names) {
    LOG(INFO) << "unable to get subject alt name extension";
    return false;
  }
  VLOG(1) << "subject alt names=" << sk_GENERAL_NAME_num(subject_alt_names);

  bool matched = false;
  for (size_t i = 0; i < sk_GENERAL_NAME_num(subject_alt_names); ++i) {
    GENERAL_NAME* subject_alt_name =
        sk_GENERAL_NAME_value(subject_alt_names, i);
    switch (subject_alt_name->type) {
      case GEN_DNS:
        VLOG(1) << "ignore dns name";
        break;

      case GEN_IPADD:
        {
          // ASN1_OCTET_STRING *iPAddress;
          unsigned char* ipaddr =
              ASN1_STRING_data(subject_alt_name->d.iPAddress);
          if (!ipaddr)
            continue;
          int len = ASN1_STRING_length(subject_alt_name->d.iPAddress);
          switch (len) {
            case 4:
              if (af == AF_INET) {
                if (memcmp(ipaddr, ap, len) == 0) {
                  matched = true;
                }
              }
              break;
            case 16:
              if (af == AF_INET6) {
                if (memcmp(ipaddr, ap, len) == 0) {
                  matched = true;
                }
              }
              break;
            default:
              LOG(WARNING) << "invalid IP address: length=" << len;
          }
        }
        break;

      default:
        LOG(INFO) << "unsupported alt name type:" << subject_alt_name->type;
        break;
    }
    if (matched) {
      break;
    }
  }
  sk_GENERAL_NAME_pop_free(subject_alt_names, GENERAL_NAME_free);
  return matched;
}


// URL should be http (not https).
void DownloadCrl(
    ScopedSocket* sock,
    const HttpRequest& req,
    HttpResponse* resp) {
  resp->Reset();

  // Send request.
  if (!sock->valid()) {
    LOG(ERROR) << "connection failure:" << *sock;
    return;
  }

  std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> request =
      req.NewStream();

  const void* data = nullptr;
  int size = 0;
  while (request->Next(&data, &size)) {
    absl::string_view buf(static_cast<const char*>(data), size);
    if (sock->WriteString(buf, kCrlIoTimeout) != OK) {
      LOG(ERROR) << "write failure:"
                 << " fd=" << *sock;
      return;
    }
  }

  for (;;) {
    char* buf;
    int buf_size;
    resp->Buffer(&buf, &buf_size);
    ssize_t len = sock->ReadWithTimeout(buf, buf_size, kCrlIoTimeout);
    if (len < 0) {
      LOG(ERROR) << "read failure:"
                 << " fd=" << *sock
                 << " len=" << len
                 << " resp has_header=" << resp->HasHeader()
                 << " resp status_code=" << resp->status_code()
                 << " resp len=" << resp->len();
      return;
    }
    if (resp->Recv(len)) {
      resp->Parse();
      return;
    }
  }
  // UNREACHABLE.
}

string GetCrlUrl(X509* x509) {
  int loc = X509_get_ext_by_NID(x509, NID_crl_distribution_points, -1);
  if (loc < 0)
    return "";
  X509_EXTENSION* ext = X509_get_ext(x509, loc);
  ASN1_OCTET_STRING* asn1_os = X509_EXTENSION_get_data(ext);
  const unsigned char* data = ASN1_STRING_data(asn1_os);
  const long data_len = ASN1_STRING_length(asn1_os);
  STACK_OF(DIST_POINT)* dps = d2i_CRL_DIST_POINTS(nullptr, &data, data_len);
  if (dps == nullptr) {
    LOG(ERROR) << "could not find distpoints in CRL.";
    return "";
  }
  string url;
  for (size_t i = 0; i < sk_DIST_POINT_num(dps) && url.empty(); i++) {
    DIST_POINT* dp = sk_DIST_POINT_value(dps, i);
    if (dp->distpoint && dp->distpoint->type == 0) {
      STACK_OF(GENERAL_NAME)* general_names = dp->distpoint->name.fullname;
      for (size_t j = 0; j < sk_GENERAL_NAME_num(general_names) && url.empty();
           j++) {
        GENERAL_NAME* general_name = sk_GENERAL_NAME_value(general_names, j);
        if (general_name->type == GEN_URI) {
          url.assign(reinterpret_cast<const char*>(general_name->d.ia5->data));
          if (url.find("http://") != 0) {
            LOG(INFO) << "Unsupported distribution point URI:" << url;
            url.clear();
            continue;
          }
        } else {
          LOG(INFO) << "Unsupported distribution point type:"
                    << general_name->type;
        }
      }
    }
  }
  sk_DIST_POINT_pop_free(dps, DIST_POINT_free);
  return url;
}

bool VerifyCrl(X509_CRL* crl, X509_STORE_CTX* store_ctx) {
  bool ok = true;
  STACK_OF(X509)* x509s = X509_STORE_get1_certs(store_ctx,
                                                X509_CRL_get_issuer(crl));
  for (size_t j = 0; j < sk_X509_num(x509s); j++) {
    EVP_PKEY *pkey;
    pkey = X509_get_pubkey(sk_X509_value(x509s, j));
    if (!X509_CRL_verify(crl, pkey)) {
      ok = false;
      break;
    }
  }
  sk_X509_pop_free(x509s, X509_free);
  return ok;
}

bool IsCrlExpired(const string& label, X509_CRL* crl,
                  int crl_max_valid_duration) {
  // Is the CRL expired?
  if (!X509_CRL_get_nextUpdate(crl) ||
      X509_cmp_current_time(X509_CRL_get_nextUpdate(crl)) <= 0) {
    LOG(INFO) << "CRL is expired: label=" << label
              << " info=" << GetHumanReadableCRL(crl);
    return true;
  }

  // Does the CRL hit max valid duration set by the user?
  if (crl_max_valid_duration >= 0) {
    ASN1_TIME* crl_last_update = X509_CRL_get_lastUpdate(crl);
    time_t t = time(nullptr) - crl_max_valid_duration;
    if (X509_cmp_time(crl_last_update, &t) < 0) {
      LOG(INFO) << "CRL is too old to use.  We need to refresh: "
                << " label=" << label
                << " crl_max_valid_duration_=" << crl_max_valid_duration
                << " info=" << GetHumanReadableCRL(crl);
      return true;
    }
  }
  return false;
}

}  // anonymous namespace

//
// OpenSSLContext
//
void OpenSSLContext::Init(
    const string& hostname,
    int crl_max_valid_duration,
    OneshotClosure* invalidate_closure) {
  AUTOLOCK(lock, &mu_);
  // To keep room to support higher version, let's allow to understand all
  // TLS protocols here, and limit min supported version below.
  // Note: if TLSv1_method is used, it won't understand TLS 1.1 or TLS 1.2.
  // See: http://www.openssl.org/docs/ssl/SSL_CTX_new.html
  ctx_ = SSL_CTX_new(TLS_method());
  CHECK(ctx_);

  // Disable legacy protocols.
  SSL_CTX_set_min_proto_version(ctx_, TLS1_VERSION);

  OpenSSLSessionCache::Setup(ctx_);

  SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
  CHECK(SSL_CTX_set_cipher_list(ctx_, kCipherList));
  // TODO: write more config to ctx_.

  OpenSSLCertificateStore::SetCertsToCTX(ctx_);
  certs_info_ = OpenSSLCertificateStore::GetTrustedCertificates();
  hostname_ = hostname;
  crl_max_valid_duration_ = crl_max_valid_duration;
  notify_invalidate_closure_ = invalidate_closure;
}

OpenSSLContext::OpenSSLContext() : is_crl_ready_(false), ref_cnt_(0) {
}

OpenSSLContext::~OpenSSLContext() {
  CHECK_EQ(ref_cnt_, 0UL);
  // The remove callback is called by SSL_CTX_free.
  // See: http://www.openssl.org/docs/ssl/SSL_CTX_sess_set_get_cb.html
  SSL_CTX_free(ctx_);

  // In case it's not called.
  if (notify_invalidate_closure_ != nullptr) {
    delete notify_invalidate_closure_;
  }
}

ScopedX509CRL OpenSSLContext::GetX509CrlsFromUrl(
    const string& url, string* crl_str) {
  LOG(INFO) << "DownloadCrl:" << url;

  HttpClient::Options options;
  if (!proxy_host_.empty()) {
    options.proxy_host_name = proxy_host_;
    options.proxy_port = proxy_port_;
  }
  options.InitFromURL(url);

  HttpRequest req;
  req.Init("GET", "", options);
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  resp.SetRequestPath(url);
  resp.SetTraceId("downloadCrl");

  SocketPool* socket_pool(OpenSSLSocketPoolCache::GetSocketPool(
      options.SocketHost(), options.SocketPort()));
  if (socket_pool == nullptr) {
    LOG(ERROR) << "Socket Pool is nullptr:"
               << " host=" << options.SocketHost()
               << " port=" << options.SocketPort();
    return nullptr;
  }

  for (size_t retry = 0;
       retry < std::max(kMaxDownloadCrlRetry, socket_pool->NumAddresses());
       ++retry) {
    ScopedSocket sock(socket_pool->NewSocket());
    if (!sock.valid()) {
      // We might have used up all candidate addresses in the pool.
      // It might be better to wait a while.
      LOG(WARNING) << "It seems to fail to connect to all available addresses."
                   << " Going to wait for a while."
                   << " kWaitForThingsGetsBetterInMs="
                   << kWaitForThingsGetsBetter;
      absl::SleepFor(kWaitForThingsGetsBetter);
      continue;
    }
    DownloadCrl(&sock, req, &resp);
    if (resp.status_code() != 200) {
      LOG(WARNING) << "download CRL retrying:"
                   << " retry=" << retry
                   << " url=" << url
                   << " http=" << resp.status_code();
      socket_pool->CloseSocket(std::move(sock), true);
      continue;
    }
    crl_str->assign(resp.parsed_body());
    ScopedX509CRL x509_crl(ParseCrl(*crl_str));
    if (x509_crl == nullptr) {
      LOG(WARNING) << "failed to parse CRL data:"
                   << " url=" << url
                   << " contents length=" << crl_str->length()
                   << " resp header=" << resp.Header();
      socket_pool->CloseSocket(std::move(sock), true);
      continue;
    }
    // we requested "Connection: close", so close the socket, but no error.
    socket_pool->CloseSocket(std::move(sock), false);
    return x509_crl;
  }

  LOG(ERROR) << "failed to download CRL from " << url;
  return nullptr;
}

bool OpenSSLContext::SetupCrlsUnlocked(STACK_OF(X509)* x509s) {
  CHECK(!is_crl_ready_);
  crls_.clear();
  std::unique_ptr<X509_STORE, ScopedX509StoreFree> store(X509_STORE_new());
  std::unique_ptr<X509_STORE_CTX, ScopedX509StoreCtxFree>
      store_ctx(X509_STORE_CTX_new());
  X509_STORE_CTX_init(store_ctx.get(), store.get(), nullptr, x509s);
  const int num_x509s = sk_X509_num(x509s);
  for (int i = 0; i < num_x509s; i++) {
    X509* x509 = sk_X509_value(x509s, i);
    string url = GetCrlUrl(x509);
    if (url.empty())
      continue;
    ScopedX509CRL crl;
    string crl_str;

    // CRL is loaded in following steps:
    // 1. try memory cache.
    // 2. try disk cache.
    // 3. download from URL.

    // Read from memory cache.
    bool is_mem_cache_used = false;
    crl = OpenSSLCRLCache::LookupCRL(url);
    if (crl) {
      if (IsCrlExpired("memory", crl.get(), crl_max_valid_duration_)) {
        OpenSSLCRLCache::DeleteCRL(url);
        crl.reset();
      }
      // Is the CRL valid?
      if (crl.get() && !VerifyCrl(crl.get(), store_ctx.get())) {
        LOG(WARNING) << "Failed to verify memory cached CRL."
                     << " url=" << url;
        OpenSSLCRLCache::DeleteCRL(url);
        crl.reset();
      }

      is_mem_cache_used = (crl.get() != nullptr);
    }

    // Read from disk cache.
    const string& cache_file =
        file::JoinPath(GetCacheDirectory(),
                       "CRL-" + NormalizeToUseFilename(url));
    bool is_disk_cache_used = false;
    if (!is_mem_cache_used && ReadFileToString(cache_file.c_str(), &crl_str)) {
      crl = ParseCrl(crl_str);
      if (crl &&
          IsCrlExpired(cache_file, crl.get(), crl_max_valid_duration_)) {
        remove(cache_file.c_str());
        crl.reset();
      }

      // Is the CRL valid?
      if (crl.get() && !VerifyCrl(crl.get(), store_ctx.get())) {
        LOG(WARNING) << "Failed to verify disk cached CRL: " << cache_file;
        remove(cache_file.c_str());
        crl.reset();
      }

      is_disk_cache_used = (crl.get() != nullptr);
    }

    // Download from URL.
    if (!is_mem_cache_used && !is_disk_cache_used) {
      crl = GetX509CrlsFromUrl(url, &crl_str);
      if (crl &&
          IsCrlExpired(url, crl.get(), crl_max_valid_duration_)) {
        crl.reset();
      }

      // Is the CRL valid?
      if (crl.get() && !VerifyCrl(crl.get(), store_ctx.get())) {
        LOG(WARNING) << "Failed to verify CRL: " << url;
        crl.reset();
      }
    }

    // Without CRL, TLS is not safe.
    if (!crl.get()) {
      std::ostringstream ss;
      ss << "CRL is not available";
      last_error_ = ss.str();
      last_error_time_ = absl::Now();
      ss << ":" << GetHumanReadableCert(x509);
      // This error may occurs if the network is broken, unstable,
      // or untrustable.
      // We believe that not running compiler_proxy is better than hiding
      // the strange situation. However, at the same time, sudden death is
      // usually difficult for users to understand what is bad.
      // Decision: die at start-time, won't die after that, but it seems
      // too late to die here.
      LOG(ERROR) << ss.str();
      return false;
    }

    X509_STORE_add_crl(SSL_CTX_get_cert_store(ctx_), crl.get());
    certs_info_.append(GetHumanReadableCRL(crl.get()));
    if (!is_mem_cache_used && !is_disk_cache_used) {
      LOG(INFO) << "CRL loaded from: " << url;
      const string& cache_dir = string(file::Dirname(cache_file));
      if (!EnsureDirectory(cache_dir, 0700)) {
        LOG(WARNING) << "Failed to create cache dir: " << cache_dir;
      }
      if (WriteStringToFile(crl_str, cache_file)) {
        LOG(INFO) << "CRL is cached to: " << cache_file;
      } else {
        LOG(WARNING) << "Failed to write CRL cache to: " << cache_file;
      }
    }
    if (is_disk_cache_used) {
      LOG(INFO) << "Read CRL from cache:"
                << " url=" << url
                << " cache_file=" << cache_file;
    }
    if (is_mem_cache_used) {
      LOG(INFO) << "loaded CRL in memory: " << url;
    } else {
      OpenSSLCRLCache::SetCRL(url, crl.get());
      // If loaded from memory, we can assume we have already shown CRL info
      // to log, and we do not show it again.
      LOG(INFO) << GetHumanReadableCRL(crl.get());
    }
    crls_.emplace_back(std::move(crl));
  }

  LOG_IF(WARNING, crls_.empty())
      << "A certificate should usually have its CRL."
      << " If we cannot not load any CRLs, something should be broken."
      << " certificates=" << GetHumanReadableCerts(x509s);

  if (!crls_.empty()) {
    VLOG(1) << "CRL is loaded.  We will check it during verification.";
    X509_VERIFY_PARAM *verify_param = X509_VERIFY_PARAM_new();
    X509_VERIFY_PARAM_set_flags(verify_param, X509_V_FLAG_CRL_CHECK);
    SSL_CTX_set1_param(ctx_, verify_param);
    X509_VERIFY_PARAM_free(verify_param);
    LOG(INFO) << "We may reject if the domain is not listed in loaded CRLs.";
  }

  is_crl_ready_ = true;
  return true;
}

bool OpenSSLContext::IsRevoked(STACK_OF(X509)* x509s) {
  AUTOLOCK(lock, &mu_);
  if (!last_error_.empty() && last_error_time_ &&
      absl::Now() < *last_error_time_ + kErrorTimeout) {
    LOG(ERROR) << "Preventing using SSL because of:" << last_error_
               << " last_error_time_=" << *last_error_time_;
    return true;
  }
  if (!is_crl_ready_ && !SetupCrlsUnlocked(x509s)) {
    LOG(ERROR) << "Failed to load CRLs:"
               << GetHumanReadableCerts(x509s);
    return true;
  }
  // Check CRLs.
  for (size_t i = 0; i < sk_X509_num(x509s); i++) {
    X509* x509 = sk_X509_value(x509s, i);
    for (size_t j = 0; j < crls_.size(); j++) {
      X509_REVOKED* rev;
      if (X509_CRL_get0_by_cert(crls_[j].get(), &rev, x509)) {
        LOG(ERROR) << "Certificate is already revoked:"
                   << GetHumanReadableCert(x509);
        return true;
      }
    }
  }
  return false;
}

/* static */
bool OpenSSLContext::IsHostnameMatched(
    absl::string_view hostname, absl::string_view pattern) {
  absl::string_view::size_type pos = pattern.find("*");
  if (pos == absl::string_view::npos && pattern == hostname) {
    return true;
  }

  absl::string_view prefix = pattern.substr(0, pos);
  absl::string_view suffix = pattern.substr(pos + 1);  // skip "*".
  VLOG(1) << "prefix=" << prefix;
  VLOG(1) << "suffix=" << suffix;
  if (!prefix.empty() && !absl::StartsWith(hostname, prefix)) {
    return false;
  }
  if (!suffix.empty() && !absl::EndsWith(hostname, suffix)) {
    return false;
  }
  absl::string_view wildcard_part = hostname.substr(
      prefix.length(),
      hostname.length() - prefix.length() - suffix.length());
  if (wildcard_part.find(".") != absl::string_view::npos) {
    return false;
  }
  return true;
}

bool OpenSSLContext::IsValidServerIdentity(X509* cert) {
  AUTOLOCK(lock, &mu_);
  struct in_addr in4;
  if (inet_pton(AF_INET, hostname_.c_str(), &in4) == 1) {
    // hostname is IPv4 addr.
    if (MatchAltIPAddress(cert, AF_INET, &in4)) {
      LOG(INFO) << "Hostname matches with IPv4 address:"
                << " hostname=" << hostname_;
      return true;
    }
    LOG(INFO) << "Hostname(IPv4) didn't match with certificate:"
              << " hostname=" << hostname_;
    return false;
  }

  struct in6_addr in6;
  if (inet_pton(AF_INET6, hostname_.c_str(), &in6) == 1) {
    // hostname is IPv6 addr.
    if (MatchAltIPAddress(cert, AF_INET6, &in6)) {
      LOG(INFO) << "Hostname matches with IPv6 address:"
                << " hostname=" << hostname_;
      return true;
    }
    LOG(INFO) << "Hostname(IPv6) didn't match with certificate:"
              << " hostname=" << hostname_;
    return false;
  }

  const std::vector<string>& sans = GetAltDNSNames(cert);
  if (sans.empty()) {
    // Subject common name is used only when dNSName is not available.
    //
    // See: http://tools.ietf.org/html/rfc2818#section-3.1
    // > If a subjectAltName extension of type dNSName is present, that MUST
    // > be used as the identity. Otherwise, the (most specific) Common Name
    // > field in the Subject field of the certificate MUST be used.
    const string& cn = GetSubjectCommonName(cert);
    if (OpenSSLContext::IsHostnameMatched(hostname_, cn)) {
      LOG(INFO) << "Hostname matches with common name:"
                << " hostname=" << hostname_
                << " cn=" << cn;
      return true;
    }
    LOG(INFO) << "Hostname didn't match with common name:"
              << " hostname=" << hostname_
              << " cn=" << cn;
    return false;
  }
  for (const auto& san : sans) {
    if (OpenSSLContext::IsHostnameMatched(hostname_, san)) {
      LOG(INFO) << "Hostname matches with subject alternative names:"
                << " hostname=" << hostname_
                << " san=" << san;
      return true;
    }
  }
  LOG(ERROR) << "Hostname did not match with certificate:"
             << " hostname=" << hostname_;
  return false;
}

void OpenSSLContext::SetProxy(const string& proxy_host, const int proxy_port) {
  proxy_host_.assign(proxy_host);
  proxy_port_ = proxy_port;
}

void OpenSSLContext::Invalidate() {
  OneshotClosure* c = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    if (notify_invalidate_closure_) {
      c = notify_invalidate_closure_;
      notify_invalidate_closure_ = nullptr;
    }
  }

  if (c) {
    c->Run();
  }
}

SSL* OpenSSLContext::NewSSL(bool* session_reused) {
  CHECK(session_reused);
  SSL* ssl = SSL_new(ctx_);
  CHECK(ssl) << "Failed on SSL_new.";

  // TLS Server Name Indication (SNI).
  DCHECK(!hostname_.empty());
  CHECK(SSL_set_tlsext_host_name(ssl, hostname_.c_str()))
      << "TLS Server Name Indication (SNI) failed:" << hostname_;
  *session_reused = true;
  if (!OpenSSLSessionCache::SetCachedSession(ctx_, ssl)) {
    LOG(INFO) << "No session is cached. We need to start from handshake."
              << " ctx=" << ctx_
              << " hostname=" << hostname_;
    *session_reused = false;
  }

  ++ref_cnt_;
  return ssl;
}

void OpenSSLContext::DeleteSSL(SSL* ssl) {
  DCHECK(ssl);
  DCHECK_GT(ref_cnt_, 0UL);
  --ref_cnt_;
  SSL_free(ssl);
}

void OpenSSLContext::RecordSession(SSL* ssl) {
  OpenSSLSessionCache::RecordSession(ssl);
}

//
// TLS Engine
//
OpenSSLEngine::OpenSSLEngine()
  : ssl_(nullptr), network_bio_(nullptr),
    want_read_(false), want_write_(false),
    recycled_(false), need_self_verify_(false),
    need_to_store_session_(false), state_(BEFORE_INIT) {}

OpenSSLEngine::~OpenSSLEngine() {
  if (ssl_ != nullptr) {
    // TODO: actually send shutdown to server not BIO.
    SSL_shutdown(ssl_);
    ctx_->DeleteSSL(ssl_);
  }
  if (network_bio_ != nullptr) {
    BIO_free_all(network_bio_);
  }
}

void OpenSSLEngine::Init(OpenSSLContext* ctx) {
  DCHECK(ctx);
  DCHECK(!ssl_);
  DCHECK_EQ(state_, BEFORE_INIT);

  // If IsCrlReady() comes after creating SSL*, ssl_ may not check CRLs
  // even if it should do.  Since loaded CRLs are cached in OpenSSLContext,
  // penalty to check it should be little.
  need_self_verify_ = !ctx->IsCrlReady();
  bool session_reused = false;
  ssl_ = ctx->NewSSL(&session_reused);
  DCHECK(ssl_);
  if (!session_reused) {
    LOG(INFO) << "Need to register session by myself."
              << " hostname=" << ctx->hostname();
    need_to_store_session_ = true;
  }
  DCHECK(ssl_);

  // Since internal_bio is free'd by SSL_free, we do not need to keep this
  // separately.
  BIO* internal_bio;
  CHECK(BIO_new_bio_pair(&internal_bio, kNetworkBufSize, &network_bio_,
                         kNetworkBufSize))
      << "BIO_new_bio_pair failed.";
  SSL_set_bio(ssl_, internal_bio, internal_bio);

  ctx_ = ctx;
  Connect();  // Do not check anything since nothing has started here.
  state_ = IN_CONNECT;
}

bool OpenSSLEngine::IsIOPending() const {
  return (state_ == IN_CONNECT) || want_read_ || want_write_;
}

int OpenSSLEngine::GetDataToSendTransport(string* data) {
  DCHECK_NE(state_, BEFORE_INIT);
  size_t max_read = BIO_ctrl(network_bio_, BIO_CTRL_PENDING, 0, nullptr);
  if (max_read > 0) {
    data->resize(max_read);
    char* buf = &((*data)[0]);
    int read_bytes = BIO_read(network_bio_, buf, max_read);
    DCHECK_GT(read_bytes, 0);
    CHECK_EQ(static_cast<int>(max_read), read_bytes);
  }
  want_write_ = false;
  if (state_ == IN_CONNECT) {
    int status = Connect();
    if (status < 0 && status != TLSEngine::TLS_WANT_READ &&
        status != TLSEngine::TLS_WANT_WRITE)
      return TLSEngine::TLS_VERIFY_ERROR;
  }
  return max_read;
}

size_t OpenSSLEngine::GetBufSizeFromTransport() {
  return BIO_ctrl_get_write_guarantee(network_bio_);
}

int OpenSSLEngine::SetDataFromTransport(const absl::string_view& data) {
  DCHECK_NE(state_, BEFORE_INIT);
  size_t max_write = BIO_ctrl_get_write_guarantee(network_bio_);
  CHECK_LE(data.size(), max_write);
  int ret = BIO_write(network_bio_, data.data(), data.size());
  CHECK_EQ(ret, static_cast<int>(data.size()));
  want_read_ = false;
  if (state_ == IN_CONNECT) {
    int status = Connect();
    if (status < 0 && status != TLSEngine::TLS_WANT_READ &&
        status != TLSEngine::TLS_WANT_WRITE)
      return TLSEngine::TLS_VERIFY_ERROR;
  }
  return ret;
}

int OpenSSLEngine::Read(void* data, int size) {
  DCHECK_EQ(state_, READY);
  int ret = SSL_read(ssl_, data, size);
  return UpdateStatus(ret);
}

int OpenSSLEngine::Write(const void* data, int size) {
  DCHECK_EQ(state_, READY);
  int ret = SSL_write(ssl_, data, size);
  return UpdateStatus(ret);
}

int OpenSSLEngine::UpdateStatus(int return_value) {
  want_read_ = false;
  want_write_ = false;
  if (return_value > 0)
    return return_value;

  int ssl_err = SSL_get_error(ssl_, return_value);
  switch (ssl_err) {
    case SSL_ERROR_WANT_READ:
      want_read_ = true;
      return TLSEngine::TLS_WANT_READ;
    case SSL_ERROR_WANT_WRITE:
      want_write_ = true;
      return TLSEngine::TLS_WANT_WRITE;
    case SSL_ERROR_SSL:
      if (SSL_get_verify_result(ssl_) != X509_V_OK) {
        // Renew CRLs in the next connection but fails for this time.
        LOG(WARNING) << "Resetting CRLs because of verify error."
                     << " details=" << X509_verify_cert_error_string(
                         SSL_get_verify_result(ssl_));
        ctx_->Invalidate();
      }
      FALLTHROUGH_INTENDED;
    default:
      LOG(ERROR) << "OpenSSL error"
                 << " ret=" << return_value
                 << " ssl_err=" << ssl_err
                 << " err_msg=" << GetLastErrorMessage();
      return TLSEngine::TLS_ERROR;
  }
}

int OpenSSLEngine::Connect() {
  int ret = SSL_connect(ssl_);
  if (ret > 0) {
    VLOG(3) << "session reused=" << SSL_session_reused(ssl_);
    state_ = READY;
    if (need_self_verify_) {
      LOG(INFO) << GetHumanReadableSSLInfo(ssl_);

      STACK_OF(X509)* x509s = SSL_get_peer_cert_chain(ssl_);
      if (!x509s) {
        LOG(ERROR) << "No x509 stored in SSL structure.";
        return TLSEngine::TLS_VERIFY_ERROR;
      }
      LOG(INFO) << GetHumanReadableCerts(x509s)
                << " session_info="
                << GetHumanReadableSessionInfo(SSL_get_session(ssl_));

      // Get server certificate to verify.
      // For ease of the code, I will not get certificate from the certificate
      // chain got above.
      std::unique_ptr<X509, ScopedX509Free> cert(
          SSL_get_peer_certificate(ssl_));
      if (cert.get() == nullptr) {
        LOG(ERROR) << "Cannot obtain the server's certificate";
        return TLSEngine::TLS_VERIFY_ERROR;
      }

      LOG(INFO) << "Checking server's identity.";
      // OpenSSL library does not check a name written in certificate
      // matches what we are connecting now.
      // We MUST do it by ourselves. Or, we allow spoofing.
      if (!ctx_->IsValidServerIdentity(cert.get())) {
        return TLSEngine::TLS_VERIFY_ERROR;
      }

      // Since CRL did not set when SSL started, CRL verification should be
      // done by myself. Note that this is usually treated by OpenSSL library.
      LOG(INFO) << "need to verify revoked certificate by myself.";
      if (ctx_->IsRevoked(x509s)) {
        return TLSEngine::TLS_VERIFY_ERROR;
      }
    }
    if (need_to_store_session_) {
      ctx_->RecordSession(ssl_);
    }
  }
  return UpdateStatus(ret);
}

string OpenSSLEngine::GetErrorString() const {
  char error_message[1024];
  ERR_error_string_n(ERR_peek_last_error(),
                     error_message, sizeof error_message);
  return error_message;
}

string OpenSSLEngine::GetLastErrorMessage() const {
  std::ostringstream oss;
  oss << GetErrorString();
  const string& ctx_err = ctx_->GetLastErrorMessage();
  if (!ctx_err.empty()) {
    oss << " ctx_error=" << ctx_err;
  }
  if (ERR_GET_REASON(ERR_peek_last_error()) ==
      SSL_R_CERTIFICATE_VERIFY_FAILED) {
    oss << " verify_error="
        << X509_verify_cert_error_string(SSL_get_verify_result(ssl_));
  }
  return oss.str();
}

OpenSSLEngineCache::OpenSSLEngineCache() :
    ctx_(nullptr), crl_max_valid_duration_(-1) {
  absl::call_once(g_openssl_init_once, InitOpenSSL);
}

OpenSSLEngineCache::~OpenSSLEngineCache() {
  // If OpenSSLEngineCache is deleted correctly, we can expect:
  // 1. all outgoing sockets are closed.
  // 2. all counterpart OpenSSLEngine instances are free'd.
  // 3. contexts_to_delete_ should be empty.
  // 4. reference count of ctx_ should be zero.
  CHECK(contexts_to_delete_.empty());
  CHECK(!ctx_.get() || ctx_->ref_cnt() == 0UL);
}

std::unique_ptr<OpenSSLEngine> OpenSSLEngineCache::GetOpenSSLEngineUnlocked() {
  if (ctx_.get() == nullptr) {
    CHECK(OpenSSLCertificateStore::IsReady())
        << "OpenSSLCertificateStore does not have any certificates.";
    ctx_ = absl::make_unique<OpenSSLContext>();
    ctx_->Init(hostname_, crl_max_valid_duration_,
               NewCallback(this, &OpenSSLEngineCache::InvalidateContext));
    if (!proxy_host_.empty())
      ctx_->SetProxy(proxy_host_, proxy_port_);
  }
  std::unique_ptr<OpenSSLEngine> engine(new OpenSSLEngine());
  engine->Init(ctx_.get());
  return engine;
}

void OpenSSLEngineCache::AddCertificateFromFile(
    const string& ssl_cert_filename) {
  OpenSSLCertificateStore::AddCertificateFromFile(ssl_cert_filename);
}

void OpenSSLEngineCache::AddCertificateFromString(
    const string& ssl_cert) {
  OpenSSLCertificateStore::AddCertificateFromString("user", ssl_cert);
}

TLSEngine* OpenSSLEngineCache::NewTLSEngine(int sock) {
  AUTOLOCK(lock, &mu_);
  auto found = ssl_map_.find(sock);
  if (found != ssl_map_.end()) {
    found->second->SetRecycled();
    return found->second.get();
  }
  std::unique_ptr<OpenSSLEngine> engine = GetOpenSSLEngineUnlocked();
  OpenSSLEngine* engine_ptr = engine.get();
  CHECK(ssl_map_.emplace(sock, std::move(engine)).second)
      << "ssl_map_ should not have the same key:" << sock;
  VLOG(1) << "SSL engine allocated. sock=" << sock;
  return engine_ptr;
}

void OpenSSLEngineCache::WillCloseSocket(int sock) {
  AUTOLOCK(lock, &mu_);
  VLOG(1) << "SSL engine release. sock=" << sock;
  auto found = ssl_map_.find(sock);
  if (found != ssl_map_.end()) {
    ssl_map_.erase(found);
  }

  if (!contexts_to_delete_.empty()) {
    std::vector<std::unique_ptr<OpenSSLContext>> new_contexts_to_delete;
    for (auto& ctx : contexts_to_delete_) {
      if (ctx->ref_cnt() == 0) {
        CHECK(ctx.get() != ctx_.get());
        continue;
      }
      new_contexts_to_delete.emplace_back(std::move(ctx));
    }
    contexts_to_delete_ = std::move(new_contexts_to_delete);
  }
}

void OpenSSLEngineCache::InvalidateContext() {
  AUTOLOCK(lock, &mu_);
  // OpenSSLContext instance should be held until ref_cnt become zero
  // i.e. no OpenSSLEngine instance use it.
  LOG_IF(ERROR, ctx_->hostname() != hostname_)
      << "OpenSSLContext hostname is different from OpenSSLEngineFactory one. "
      << " It might be changed after ctx_ is created?"
      << " ctx=" << ctx_->hostname()
      << " factory=" << hostname_;
  contexts_to_delete_.emplace_back(std::move(ctx_));
}

}  // namespace devtools_goma
