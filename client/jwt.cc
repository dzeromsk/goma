// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jwt.h"

#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/digest.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "base64.h"
#include "glog/logging.h"
#include "ioutil.h"

namespace devtools_goma {

namespace {

// A descriptor of the intended target of the assertion.
// When making an access token request this value is always
// https://www.googleapis.com/oauth2/v4/token.
// https://developers.google.com/identity/protocols/OAuth2ServiceAccount#authorizingrequests
constexpr char kAssertionTarget[] =
    "https://www.googleapis.com/oauth2/v4/token";

// Time until access token will expire.
constexpr absl::Duration kExpiresIn = absl::Hours(1);

std::string OpenSSLErrorString(uint32_t err) {
  char buf[1024];
  ERR_error_string_n(err, buf, sizeof(buf));
  return buf;
}

}  // namespace

/* static */
std::unique_ptr<JsonWebToken::Key> JsonWebToken::Key::Load(
    const std::string& pem_key) {
  ScopedBIO bio(BIO_new_mem_buf(pem_key.data(), pem_key.size()));
  ScopedEVP_PKEY pkey(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    return nullptr;
  }
  LOG_IF(WARNING, EVP_PKEY_id(pkey.get()) != EVP_PKEY_RSA)
      << "load non RSA key. id=" << EVP_PKEY_id(pkey.get());
  return std::unique_ptr<JsonWebToken::Key>(new Key(std::move(pkey)));
}

std::string JsonWebToken::Key::Sign(const std::string& input) const {
  ScopedMDCTX mctx(EVP_MD_CTX_create());
  const EVP_MD* md = EVP_sha256();
  if (!EVP_DigestSignInit(mctx.get(), nullptr, md, nullptr, pkey_.get())) {
    LOG(ERROR) << "Failed to DigestSignInit:"
               << OpenSSLErrorString(ERR_get_error());
    return "";
  }
  EVP_DigestSignUpdate(mctx.get(), input.data(), input.size());
  size_t siglen = 0;
  if (!EVP_DigestSignFinal(mctx.get(), nullptr, &siglen)) {
    LOG(ERROR) << "Failed to get siglen:"
               << OpenSSLErrorString(ERR_get_error());
    return "";
  }
  std::string sig;
  sig.resize(siglen);
  if (!EVP_DigestSignFinal(mctx.get(),
                           reinterpret_cast<uint8_t*>(&sig[0]), &siglen)) {
    LOG(ERROR) << "Failed to get sig:"
               << OpenSSLErrorString(ERR_get_error());
    return "";
  }
  sig.resize(siglen);
  return sig;
}

JsonWebToken::JsonWebToken(ClaimSet claim_set)
    : claim_set_(std::move(claim_set)) {}

JsonWebToken::~JsonWebToken() {
}

std::string JsonWebToken::Token(const Key& key) const {
  return TokenWithTimestamp(key, absl::Now());
}

std::string JsonWebToken::TokenWithTimestamp(const Key& key,
                                             absl::Time timestamp) const {
  std::string header = CreateHeaderJson();
  std::string claim_set = CreateClaimSetJson(claim_set_, timestamp);
  std::string base_string = CreateTokenBaseString(header, claim_set);
  std::string sig = Sign(base_string, key);
  if (sig.empty()) {
    return "";
  }
  return CreateToken(base_string, sig);
}

/* static */
std::string JsonWebToken::CreateHeaderJson() {
  // Service accounts rely on the RSA SHA-256 algorithm and the JWT token
  // format.
  return "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
}

/* static */
std::string JsonWebToken::CreateClaimSetJson(const ClaimSet& cs,
                                             absl::Time timestamp) {
  std::stringstream ss;
  ss << "{";
  ss << "\"iss\":" << EscapeString(cs.iss);
  if (!cs.sub.empty()) {
    ss << ",\"sub\":" << EscapeString(cs.sub);
  }
  ss << ",\"scope\":" << EscapeString(absl::StrJoin(cs.scopes, " "));
  ss << ",\"aud\":" << EscapeString(kAssertionTarget);
  ss << ",\"exp\":" << absl::ToTimeT(timestamp + kExpiresIn);
  ss << ",\"iat\":" << absl::ToTimeT(timestamp);
  ss << "}";
  return ss.str();
}

/* static */
std::string JsonWebToken::CreateTokenBaseString(
    const std::string& header, const std::string& claim_set) {
  std::stringstream ss;
  ss << Base64UrlEncode(header, false);
  ss << ".";
  ss << Base64UrlEncode(claim_set, false);
  return ss.str();
}

/* static */
std::string JsonWebToken::Sign(const std::string& base_string, const Key& key) {
  return key.Sign(base_string);
}

/* static */
std::string JsonWebToken::CreateToken(const std::string& base_string,
                                      const std::string& signature_bytes) {
  std::stringstream ss;
  ss << base_string;
  ss << ".";
  ss << Base64UrlEncode(signature_bytes, false);
  return ss.str();
}

const char JsonWebToken::kGrantTypeEncoded[] =
        "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer";

}  // namespace devtools_goma
