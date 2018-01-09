// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "openssl_engine_helper.h"

#include <string>

#include <Security/SecImportExport.h>
#include <Security/SecKeychainSearch.h>
#include <Security/SecKey.h>
#include <Security/SecCertificate.h>
#include <CoreFoundation/CoreFoundation.h>

#include "glog/logging.h"

using std::string;

namespace {

// http://www.apple.com/certificateauthority/ca_program.html
const char* kRootKeychainStore = "/System/Library/Keychains/X509Anchors";

// Searches / retrieves certificates.
// TODO: replace several deprecated functions, which I could not
//                    find alternative functions to use.
OSStatus ReadCertsFromKeychain(SecKeychainRef chain_ref,
                               CFMutableArrayRef out_array,
                               int *num_items) {
  OSStatus ret;
  SecKeychainSearchRef search_ref;

  ret = SecKeychainSearchCreateFromAttributes(chain_ref,
                                              kSecCertificateItemClass,
                                              nullptr,
                                              &search_ref);
  if (ret)
    return ret;

  for (;;) {
    SecKeychainItemRef item_ref;
    ret = SecKeychainSearchCopyNext(search_ref, &item_ref);
    if (ret) {
      if (ret == errSecItemNotFound)
        ret = noErr;
      break;
    }
    CFArrayAppendValue(out_array, item_ref);
    CFRelease(item_ref);
    (*num_items)++;
  }
  CFRelease(search_ref);

  return ret;
}

// Converts certificates to string with PEM format.
OSStatus WriteCertsToMemory(CFMutableArrayRef export_items, string* certs) {
  CFDataRef export_data;
  OSStatus ret;
  ret = SecKeychainItemExport(export_items, kSecFormatPEMSequence,
      kSecItemPemArmour, nullptr, &export_data);
  if (ret)
    return ret;

  certs->assign(reinterpret_cast<const char*>(CFDataGetBytePtr(export_data)),
                CFDataGetLength(export_data));
  CFRelease(export_data);

  return ret;
}

// Dumps the system root certificates to |out_certs| with PEM format.
bool DumpCertificates(SecKeychainRef keychain_ref, string* out_certs) {
  OSStatus ret;
  int num_certs;
  CFMutableArrayRef export_items = CFArrayCreateMutable(
      nullptr, 0, &kCFTypeArrayCallBacks);
  if (export_items == nullptr) {
    LOG(ERROR) << "Failed to allocate memory for certificates.";
    return false;
  }

  ret = ReadCertsFromKeychain(keychain_ref, export_items, &num_certs);
  if (ret) {
    LOG(ERROR) << "Failed to read root certificates keychain:"
               << " ret=" << ret;
    CFRelease(export_items);
    return false;
  }

  ret = WriteCertsToMemory(export_items, out_certs);
  if (ret) {
    LOG(ERROR) << "Failed to copy root certificates keychain:"
               << " ret=" << ret;
    CFRelease(export_items);
    return false;
  }

  CFRelease(export_items);
  return true;
}

}  // namespace

namespace devtools_goma {

bool GetTrustedRootCerts(string* certs) {
  OSStatus err_status;
  SecKeychainRef keychain_ref;
  bool ret = false;

  err_status = SecKeychainOpen(kRootKeychainStore, &keychain_ref);
  if (err_status) {
    LOG(ERROR) << "Failed to open root certificates keychain:"
               << kRootKeychainStore
               << " ret=" << err_status;
    return false;
  }
  ret = DumpCertificates(keychain_ref, certs);
  CFRelease(keychain_ref);

  return ret;
}

}  // namespace devtools_goma
