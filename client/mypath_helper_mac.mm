// Copyright 2018 Google Inc. All Rights Reserved.

#include "mypath_helper.h"

#import <Foundation/Foundation.h>

namespace devtools_goma {

string GetPlatformSpecificTempDirectory() {
  NSString* dir = NSTemporaryDirectory();
  if (dir == nil) {
    return string();
  }
  return string([dir UTF8String]);
}

}  // namespace devtools_goma
