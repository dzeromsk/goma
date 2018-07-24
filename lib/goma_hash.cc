// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "goma_hash.h"

#include <stdio.h>


#if defined __MACH__
# include <CommonCrypto/CommonDigest.h>
#elif defined _WIN32
# include "config_win.h"
# include <wincrypt.h>
# pragma comment(lib, "advapi32.lib")
# include "absl/strings/string_view.h"
# define SHA256_DIGEST_LENGTH 32
#else
# include <openssl/sha.h>  // BoringSSL
# ifndef OPENSSL_IS_BORINGSSL
#  error "We expect BoringSSL in the third_party directory is used."
# endif
#endif
#include "file_helper.h"
#include "glog/logging.h"
using std::string;

namespace {

bool FromHexChar(char c, unsigned char* ret) {
  if ('0' <= c && c <= '9') {
    *ret = c - '0';
    return true;
  }
  if ('a' <= c && c <= 'f') {
    *ret = c - 'a' + 10;
    return true;
  }
  if ('A' <= c && c <= 'F') {
    *ret = c - 'A' + 10;
    return true;
  }

  return false;
}

}  // anonymous namespace

namespace devtools_goma {

bool SHA256HashValue::ConvertFromHexString(const string& hex_string,
                                           SHA256HashValue* hash_value) {
  if (hex_string.size() != 64U) {
    return false;
  }

  for (size_t i = 0; i < 32; ++i) {
    unsigned char c1, c2;
    if (!FromHexChar(hex_string[2 * i], &c1)) {
      return false;
    }
    if (!FromHexChar(hex_string[2 * i + 1], &c2)) {
      return false;
    }
    hash_value->data_[i] = (c1 << 4) + c2;
  }

  return true;
}

string SHA256HashValue::ToHexString() const {
  string md_str;
  for (size_t i = 0; i < 32; ++i) {
    char hex[3];
    hex[0] = "0123456789abcdef"[(data_[i] >> 4) & 0x0f];
    hex[1] = "0123456789abcdef"[data_[i] & 0x0f];
    hex[2] = '\0';
    md_str += hex;
  }

  return md_str;
}

size_t SHA256HashValue::Hash() const {
  size_t v = 0;
  for (int i = 0; i < sizeof(data_); ++i) {
    v = v * 37 + data_[i];
  }
  return v;
}

void ComputeDataHashKeyForSHA256HashValue(absl::string_view data,
                                          SHA256HashValue* hash_value) {
#ifdef __MACH__
  CC_SHA256_CTX sha256;
  CC_SHA256_Init(&sha256);
  CC_SHA256_Update(&sha256, data.data(), data.size());
  CC_SHA256_Final(hash_value->mutable_data(), &sha256);
#elif defined _WIN32
  HCRYPTPROV provider;
  HCRYPTHASH hash;

  if (!CryptAcquireContext(&provider, nullptr, nullptr, PROV_RSA_AES,
                           CRYPT_VERIFYCONTEXT)) {
    LOG(FATAL) << "Unable to acquire RSA_AES provider";
    return;
  }
  if (CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
    if (CryptHashData(hash, reinterpret_cast<const BYTE*>(data.data()),
                      data.size(), 0)) {
      DWORD hash_size = SHA256_DIGEST_LENGTH;
      CryptGetHashParam(hash, HP_HASHVAL, hash_value->mutable_data(),
                        &hash_size, 0);
    }
  }
  if (hash) {
    CryptDestroyHash(hash);
  }
  if (provider) {
    CryptReleaseContext(provider, 0);
  }
#else
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, data.data(), data.size());
  SHA256_Final(hash_value->mutable_data(), &sha256);
#endif
}

void ComputeDataHashKey(absl::string_view data, string* md_str) {
  SHA256HashValue value;
  ComputeDataHashKeyForSHA256HashValue(data, &value);
  *md_str = value.ToHexString();
}

bool GomaSha256FromFile(const string& filename, string* md_str) {
  string s;
  if (!ReadFileToString(filename, &s)) return false;
  ComputeDataHashKey(s, md_str);
  return true;
}

}  // namespace devtools_goma
