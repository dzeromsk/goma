// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "json_util.h"

namespace devtools_goma {

bool GetStringFromJson(const Json::Value& json, const std::string& key,
                       std::string* value, std::string* error_message) {
  if (!json.isMember(key)) {
    *error_message = "missing " + key;
    return false;
  }

  const Json::Value& str_value = json[key];
  if (!str_value.isString()) {
    *error_message = key + " is not string";
    return false;
  }

  *value = str_value.asString();
  return true;
}

bool GetNonEmptyStringFromJson(const Json::Value& json, const std::string& key,
                               std::string* value, std::string* error_message) {
  if (!GetStringFromJson(json, key, value, error_message)) {
    return false;
  }

  if (value->empty()) {
    *error_message = key + " is empty";
    return false;
  }

  return true;
}

bool GetIntFromJson(const Json::Value& json, const std::string& key,
                    int* value, std::string* error_message) {
  if (!json.isMember(key)) {
    *error_message = "missing " + key;
    return false;
  }

  const Json::Value& int_value = json[key];
  if (!int_value.isInt()) {
    *error_message = key + " is not int";
    return false;
  }

  *value = int_value.asInt();
  return true;
}

bool GetInt64FromJson(const Json::Value& json, const std::string& key,
                      int64_t* value, std::string* error_message) {
  if (!json.isMember(key)) {
    *error_message = "missing " + key;
    return false;
  }

  const Json::Value& int64_value = json[key];
  if (!int64_value.isInt64()) {
    *error_message = key + " is not int64";
    return false;
  }

  *value = int64_value.asInt64();
  return true;
}

} // namespace devtools_goma
