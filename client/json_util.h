// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_JSON_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_JSON_UTIL_H_

#include <string>

#include "json/json.h"

namespace devtools_goma {

// Sets the value of |key| in |json| to |value|.
// Returns true if succeeded.
// Returns false if the key is missing or the value is not string.
bool GetStringFromJson(const Json::Value& json, const std::string& key,
                       std::string* value, std::string* error_message);

// Same as GetStringFromJson. Additionally check the value is not empty.
// If the value is empty, false is returned, and |error_message| is set.
bool GetNonEmptyStringFromJson(const Json::Value& json, const std::string& key,
                               std::string* value, std::string* error_message);

bool GetIntFromJson(const Json::Value& json, const std::string& key,
                    int* value, std::string* error_message);

bool GetInt64FromJson(const Json::Value& json, const std::string& key,
                      int64_t* value, std::string* error_message);

// Attempts to fetch an array from |json| using |key| and store it in |*value|.
// Returns true if successful. Returns false if |key| was not found, or if the
// value was not an array.
bool GetArrayFromJson(const Json::Value& json, const std::string& key,
                      Json::Value* value, std::string* error_message);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_JSON_UTIL_H_
