// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "openssl_engine.h"

#include <openssl/dh.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <memory>
#include <string>

#include "glog/logging.h"
#include "gtest/gtest.h"
#include "string_piece.h"
#include "unittest_util.h"

using std::string;

namespace {

static const size_t kBufsize = 4096;
/*
 * How to generate following PEM?
 *
 * 1. generate private key.
 * $ openssl genpkey -out k.pem -algorithm rsa
 * (or, EC key like google.com)
 * $ openssl ecparam -genkey -out k.pem -name prime256v1
 * $ openssl pkcs8 -topk8 -in k.pem -out key.pem -nocrypt
 *
 * 2. generate self-signed certificate. (with SHA256 signature)
 * $ openssl req -new -x509 -key key.pem -out cert.pem -days 36500 \
 *   -sha256 -config test/openssl.cnf
 *
 * 3. copy generated key.pem and cert.pem to tests directory.
 * $ cp {key,cert}.pem /where/you/have/tests
 */
static const char* kCert = "cert.pem";
static const char* kKey = "key.pem";
static const char* kAnotherCert = "another_cert.pem";
static const char* kAnotherKey = "another_key.pem";

/*
 * How to generate following PEM?
 * 1. go run $GOROOT/src/crypto/tls/generate_cert.go -ca \
 *    -duration $((100 * 365 * 24))h \
 *    -host 127.0.0.1,::1
 * 2. cp {key,cert}.pem /where/you/have/tests
 */
static const char* kCertIPAddr = "cert_127.0.0.1.pem";
static const char* kKeyIPAddr = "key_127.0.0.1.pem";

/*
 * To generate DHParam PEM:
 * $ openssl dhparam 1024 > dhparam.pem
 */
static const char* kDHParam = "dhparam.pem";
static const int kDummyFd = 123;

class ScopedSSLCtxFree {
 public:
  inline void operator()(SSL_CTX* x) const { if (x) SSL_CTX_free(x); }
};

void SetupEphemeralDH(SSL_CTX* ctx) {
  // For DHE.
  // Since creating a DH param takes certain long time, let us read a
  // pre-computed param from a file.
  // Note that creating ECDH param is quick.
  BIO* bio =
      BIO_new_file(devtools_goma::GetTestFilePath(kDHParam).c_str(), "r");
  CHECK(bio);
  DH* dh = PEM_read_bio_DHparams(bio, nullptr, nullptr, nullptr);
  CHECK(dh);
  CHECK(SSL_CTX_set_tmp_dh(ctx, dh));
  DH_free(dh);
  CHECK(BIO_free(bio));
}

SSL_CTX* SetupServerContext(const char* cert_file, const char* key_file) {
  SSL_CTX* s_ctx = SSL_CTX_new(SSLv23_method());
  CHECK(s_ctx);
  CHECK(SSL_CTX_use_certificate_file(
      s_ctx, devtools_goma::GetTestFilePath(cert_file).c_str(),
      SSL_FILETYPE_PEM));
  CHECK(SSL_CTX_use_PrivateKey_file(
      s_ctx, devtools_goma::GetTestFilePath(key_file).c_str(),
      SSL_FILETYPE_PEM));
  CHECK(SSL_CTX_set_default_verify_paths(s_ctx));
  SetupEphemeralDH(s_ctx);
  return s_ctx;
}

class OpenSSLServerEngine {
 public:
  explicit OpenSSLServerEngine(SSL_CTX* ctx) : need_retry_(false) {
    ssl_ = SSL_new(ctx);
    CHECK(ssl_);

    // initialize BIOs.
    CHECK(BIO_new_bio_pair(&internal_bio_, kBufsize, &network_bio_, kBufsize));

    SSL_set_bio(ssl_, internal_bio_, internal_bio_);
    state_ = IN_ACCEPT;
    Accept();
  }

  ~OpenSSLServerEngine() {
    SSL_free(ssl_);
    CHECK(BIO_free(network_bio_));
  }

  string GetErrorMessage() const {
    char errmsg[1024];
    ERR_error_string_n(ERR_peek_last_error(), errmsg, sizeof(errmsg));
    return errmsg;
  }

  void UpdateStatus(int return_value) {
    need_retry_ = false;
    int ssl_err = SSL_get_error(ssl_, return_value);
    switch (ssl_err) {
      case SSL_ERROR_WANT_READ:
        need_retry_ = true;
        return;
      case SSL_ERROR_WANT_WRITE:
        need_retry_ = true;
        return;
      default:
        LOG(INFO) << "OpenSSL error"
                   << " ret=" << return_value
                   << " ssl_err=" << ssl_err
                   << " error_message=" << GetErrorMessage();
    }
  }

  void Accept() {
    int ret = SSL_accept(ssl_);
    if (ret > 0)
      state_ = READY;
    UpdateStatus(ret);
  }

  int GetSizeToSend() {
    return BIO_ctrl(network_bio_, BIO_CTRL_PENDING, 0, nullptr);
  }

  int GetDataToSendTransport(string* data, size_t acceptable_size) {
    char buf[kBufsize];
    if (acceptable_size > sizeof(buf))
      acceptable_size = sizeof(buf);
    int r = BIO_read(network_bio_, buf, acceptable_size);
    if (r > 0) {
      data->assign(buf, r);
    }
    if (state_ == IN_ACCEPT) {
      Accept();
    }
    return r;
  }

  int GetSpaceForDataFromTransport() {
    return BIO_ctrl_get_read_request(network_bio_);
  }

  int SetDataFromTransport(StringPiece data) {
    int r = BIO_write(network_bio_, data.data(), data.size());
    if (state_ == IN_ACCEPT) {
      Accept();
    }
    return r;
  }

  int Read(string* data) {
    char tmp[kBufsize];
    int r = SSL_read(ssl_, tmp, sizeof(tmp));
    if (r > 0) {
      data->assign(tmp, r);
    }
    UpdateStatus(r);
    return r;
  }

  int Write(StringPiece message) {
    int r = SSL_write(ssl_, message.data(), message.size());
    UpdateStatus(r);
    return r;
  }

  bool CanRetry() {
    return need_retry_;
  }

  bool InInit() {
    return SSL_in_init(ssl_) != 0;
  }

  string StateString() {
    return SSL_state_string_long(ssl_);
  }

 private:
  SSL* ssl_;
  BIO* internal_bio_;
  BIO* network_bio_;

  enum ServerEngineStatus { IN_ACCEPT, READY } state_;
  bool need_retry_;

 DISALLOW_COPY_AND_ASSIGN(OpenSSLServerEngine);
};

}  // namespace

namespace devtools_goma {

class OpenSSLEngineTest : public :: testing::Test {
 protected:
  void SetUp() override {
    OpenSSLEngineCache* openssl_engine_cache = new OpenSSLEngineCache;
    openssl_engine_cache->AddCertificateFromFile(GetTestFilePath(kCert));
    openssl_engine_cache->SetHostname("clients5.google.com");
    factory_.reset(openssl_engine_cache);
  }

  void TearDown() override {
    engine_ = nullptr;
    if (factory_.get()) {
      factory_->WillCloseSocket(kDummyFd);
      factory_.reset();
    }
  }

  void AddCertificateFromFile(const string& filename) {
    static_cast<OpenSSLEngineCache*>(factory_.get())->AddCertificateFromFile(
        GetTestFilePath(filename));
  }

  void SetHostname(const string& hostname) {
    factory_->SetHostname(hostname);
  }

  void SetupEngine() {
    engine_ = factory_->NewTLSEngine(kDummyFd);
  }

  void TearDownEngine() {
    if (engine_ != nullptr) {
      factory_->WillCloseSocket(kDummyFd);
      engine_ = nullptr;
    }
  }

  bool Communicate(SSL_CTX* server_ctx) {
    static const size_t kMaxIterate = 64;

    OpenSSLServerEngine server_engine(server_ctx);
    SetupEngine();
    bool s_sent = false;
    bool c_sent = false;
    bool s_recv = false;
    bool c_recv = false;

    // TODO: simulate HTTP communication.
    // To do that, we need buffering I/O.
    // Also, |server_engine| will not send any data to transport layer without
    // writing something to it.  Writing "" seems to be fine, though.
    for (size_t i = 0; i < kMaxIterate; ++i) {
      // Server: Send to client.
      if (!s_sent) {
        const string msg = "Hello From Server";
        int r = server_engine.Write(msg);
        if (r < 0 && !server_engine.CanRetry()) {
          LOG(ERROR) << "Did not send server data but could not retry.";
          return false;
        }
        if (r > 0) {
          s_sent = true;
          VLOG(1) << "server sent.";
        }
      }

      // Server: receive from client.
      if (!s_recv) {
        string data;
        int r = server_engine.Read(&data);
        if (r > 0) {
          VLOG(1) << "Sever received: " << data;
          s_recv = true;
        }
      }

      // Client: Send to server.
      if (!c_sent && !engine_->IsIOPending()) {
        const string msg = "Hello From Client";
        int r = engine_->Write(msg.c_str(), msg.length());
        if (r > 0) {
          c_sent = true;
          VLOG(1) << "client sent.";
        }
      }

      // Client: receive from server.
      if (!c_recv && !engine_->IsIOPending()) {
        char tmp[kBufsize];
        int r = engine_->Read(tmp, sizeof(tmp));
        if (r > 0) {
          c_recv = true;
          VLOG(1) << "Client received:" << string(tmp, r);
        }
      }

      // Both ways communication succeeded.
      if (s_recv && c_recv)
        return true;

      VLOG_IF(1, server_engine.InInit())
          << "server waiting in SSL_accept: "
          << server_engine.StateString();

      /* server to client */
      size_t r1, r2;
      do {
        r1 = server_engine.GetSizeToSend();
        r2 = engine_->GetBufSizeFromTransport();
        VLOG(2) << " r1=" << r1 << " r2=" << r2;
        size_t num = r1;
        if (r2 < num)
          num = r2;
        if (num) {
          string data;
          int r = server_engine.GetDataToSendTransport(&data, num);
          CHECK_GT(r, 0);
          CHECK_LE(r, static_cast<int>(num));
          CHECK_EQ(r, static_cast<int>(data.size()));
          /* possibly r < num (non-contiguous data) */
          VLOG(3) << "data=" << data;
          r = engine_->SetDataFromTransport(data);
          if (r < 0)
            return false;
          if (static_cast<int>(num) != r) {
            LOG(ERROR) << "SetDataFromTransport should accept the data size "
                       << "that is mentioned with GetBufSizeFromTransport."
                       << " num=" << num
                       << " r=" << r;
            return false;
          }
        }
      } while (r1 && r2);

      /* client to server */
      {
        string data;
        size_t r3 = engine_->GetDataToSendTransport(&data);
        if (r3) {
          CHECK_EQ(r3, data.size());
          int r = server_engine.SetDataFromTransport(data);
          VLOG(3) << "r=" << r << " data.size=" << data.size();
          CHECK_EQ(static_cast<int>(r3), r)
              << "For ease of implementation, we expect |server_engine| accept "
              << " all data from the client."
              << " If you find the test won't pass here, please "
              << " revise the code to accept it."
              << " r3=" << r3
              << " r=" << r;
        }
      }
    }
    LOG(ERROR) << "Hit max iterate."
               << " kMaxIterate=" << kMaxIterate;
    return false;
  }

 private:
  TLSEngine* engine_;
  std::unique_ptr<TLSEngineFactory> factory_;
};

TEST_F(OpenSSLEngineTest, SuccessfulCommunication) {
  // Get SSL_CTX having the certificate set in the client.
  std::unique_ptr<SSL_CTX, ScopedSSLCtxFree> s_ctx(
      SetupServerContext(kCert, kKey));
  EXPECT_TRUE(Communicate(s_ctx.get()));
}

TEST_F(OpenSSLEngineTest, VerifyByIPAddr) {
  // Get SSL_CTX having the certificate set in the client.
  std::unique_ptr<SSL_CTX, ScopedSSLCtxFree> s_ctx(
      SetupServerContext(kCertIPAddr, kKeyIPAddr));

  AddCertificateFromFile(kCertIPAddr);
  // Set the hostname that matches the certificate subjectAltName ip Address.
  SetHostname("127.0.0.1");

  EXPECT_TRUE(Communicate(s_ctx.get()));
}

TEST_F(OpenSSLEngineTest, VerifyByIPv6Addr) {
  // Get SSL_CTX having the certificate set in the client.
  std::unique_ptr<SSL_CTX, ScopedSSLCtxFree> s_ctx(
      SetupServerContext(kCertIPAddr, kKeyIPAddr));

  AddCertificateFromFile(kCertIPAddr);
  // Set the hostname that matches the certificate subjectAltName ip Address.
  SetHostname("::1");

  EXPECT_TRUE(Communicate(s_ctx.get()));
}

TEST_F(OpenSSLEngineTest, VerifyErrorByIPAddrMismatch) {
  // Get SSL_CTX having the certificate set in the client.
  std::unique_ptr<SSL_CTX, ScopedSSLCtxFree> s_ctx(
      SetupServerContext(kCertIPAddr, kKeyIPAddr));

  AddCertificateFromFile(kCertIPAddr);
  // Set the hostname that doesn't match the certificate subjectAltName
  // ip Address.
  SetHostname("192.168.0.1");

  EXPECT_FALSE(Communicate(s_ctx.get()));
}

TEST_F(OpenSSLEngineTest, VerifyErrorByIPv6AddrMismatch) {
  // Get SSL_CTX having the certificate set in the client.
  std::unique_ptr<SSL_CTX, ScopedSSLCtxFree> s_ctx(
      SetupServerContext(kCertIPAddr, kKeyIPAddr));

  AddCertificateFromFile(kCertIPAddr);
  // Set the hostname that doesn't match the certificate subjectAltName
  // ip Address.
  SetHostname("fe80::42a8:f0ff:fe44:ffe6");

  EXPECT_FALSE(Communicate(s_ctx.get()));
}

TEST_F(OpenSSLEngineTest, VerifyErrorByHostMismatch) {
  // Get SSL_CTX having the certificate set in the client.
  std::unique_ptr<SSL_CTX, ScopedSSLCtxFree> s_ctx(
      SetupServerContext(kCert, kKey));

  // Set the hostname that does not match the certificate subjectAltName.
  SetHostname("www.googleusercontent.com");

  EXPECT_FALSE(Communicate(s_ctx.get()));
  // OpenSSL engine should not bypass the check for the same context
  // in the second time.
  TearDownEngine();
  EXPECT_FALSE(Communicate(s_ctx.get()));
}

TEST_F(OpenSSLEngineTest, VerifyError) {
  // Get SSL_CTX having the certificate not used in the client.
  std::unique_ptr<SSL_CTX, ScopedSSLCtxFree> s_ctx(
      SetupServerContext(kAnotherCert, kAnotherKey));

  EXPECT_FALSE(Communicate(s_ctx.get()));
}

TEST(OpenSSLContext, IsHostnameMatched) {
  EXPECT_TRUE(
      OpenSSLContext::IsHostnameMatched(
          "clients5.google.com", "clients5.google.com"));
  EXPECT_TRUE(OpenSSLContext::IsHostnameMatched("foo.a.com", "*.a.com"));
  EXPECT_FALSE(OpenSSLContext::IsHostnameMatched("bar.foo.a.com", "*.a.com"));
  EXPECT_TRUE(OpenSSLContext::IsHostnameMatched("foo.com", "f*.com"));
  EXPECT_FALSE(OpenSSLContext::IsHostnameMatched("bar.com", "f*.com"));
}

}  // namespace devtools_goma
