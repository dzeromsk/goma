// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_JWT_H_
#define DEVTOOLS_GOMA_CLIENT_JWT_H_

#include <openssl/bio.h>
#include <openssl/digest.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "basictypes.h"
#include "glog/logging.h"
#include "gtest/gtest_prod.h"

namespace devtools_goma {

// JsonWebToken creates JWT from claim set and key.
// https://developers.google.com/identity/protocols/OAuth2ServiceAccount#authorizingrequests
class JsonWebToken {
 public:
  struct ClaimSet {
    // The email address of the service account.
    std::string iss;
    // The email address of the user for which the application is
    // requesting delegated access (if any).
    std::string sub;

    // The permissions that the application requests.
    std::vector<std::string> scopes;
  };
  // Key is private key to sign.
  class Key {
   public:
    ~Key() {}
    // Load loads PEM formatted text representation key.
    // Returns nullptr if failed.
    static std::unique_ptr<Key> Load(const std::string& pem_key);

    // Sign signs input, and returns raw signature bytes.
    std::string Sign(const std::string& input) const;

   private:
    // TODO: create openssl_util?
    template<typename T, void (*func)(T*)>
    struct Deleter {
      void operator()(T* obj) {
        func(obj);
      }
    };
    struct BIODeleter {
      void operator()(BIO* obj) {
        int r = BIO_free(obj);
        LOG_IF(ERROR, r != 1) << "Failed to BIO_free " << obj;
      }
    };
    typedef std::unique_ptr<BIO, BIODeleter> ScopedBIO;
    typedef std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>
        ScopedEVP_PKEY;
    typedef std::unique_ptr<EVP_MD_CTX,
                            Deleter<EVP_MD_CTX, EVP_MD_CTX_destroy>>
        ScopedMDCTX;

    explicit Key(ScopedEVP_PKEY pkey) : pkey_(std::move(pkey)) {}

    const ScopedEVP_PKEY pkey_;

    DISALLOW_COPY_AND_ASSIGN(Key);
  };

  explicit JsonWebToken(ClaimSet claim_set);
  ~JsonWebToken();

  // LoadKey returns a Key from pem_key string.
  // Returns nullptr if failed.
  static std::unique_ptr<Key> LoadKey(const std::string& pem_key) {
    return Key::Load(pem_key);
  }

  // Token generates JWT, including signature, signed by key, with the current
  // time as the timestamp  .
  std::string Token(const Key& key) const;

  static const char kGrantTypeEncoded[];
 private:
  friend class JsonWebTokenTest;
  FRIEND_TEST(JsonWebTokenTest, CreateClaimSetJson);
  FRIEND_TEST(JsonWebTokenTest, CreateTokenBaseString);
  FRIEND_TEST(JsonWebTokenTest, TokenWithTimestamp);

  // helper functions.

  // Token generates JWT, including signature, signed by key, with a timestamp.
  std::string TokenWithTimestamp(const Key& key, absl::Time timestamp) const;

  // CreateHeaderJson returns JSON representation of JWT header.
  static std::string CreateHeaderJson();

  // CreateClaimSetJson returns JSON representation of JWT claim set with a
  // timestamp.
  static std::string CreateClaimSetJson(const ClaimSet& cs,
                                        absl::Time timestamp);

  // CreateTokenBaseString returns JWT token's base string, which will be
  // a base string, i.e. an input for Sign.
  // i.e. {Base64url encoded header}.{Base64url encoded claim set}.
  static std::string CreateTokenBaseString(const std::string& header,
                                           const std::string& claim_set);

  // Sign returns signature bytes for base_string.
  static std::string Sign(const std::string& base_string, const Key& key);

  // CreateToken returns JWT token, from base_string and its signature bytes.
  static std::string CreateToken(const std::string& base_string,
                                 const std::string& signature_bytes);

  const ClaimSet claim_set_;

  DISALLOW_COPY_AND_ASSIGN(JsonWebToken);
};


}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_JWT_H_
