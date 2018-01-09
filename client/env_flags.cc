// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "env_flags.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>
#include <string>
#include <sstream>

using std::string;

struct GomaAutoConfigurer {
  GomaAutoConfigurer(string (*GetConfiguredValue)(void),
                     void (*SetConfiguredValue)(void))
      : GetConfiguredValue(GetConfiguredValue),
        SetConfiguredValue(SetConfiguredValue) {}

  string (*GetConfiguredValue)(void);
  void (*SetConfiguredValue)(void);
};

static std::set<string>* g_env_flag_names;
typedef std::map<string, GomaAutoConfigurer> AutoConfigurerMap;
static AutoConfigurerMap* g_autoconfigurers;

void RegisterEnvFlag(const char* name) {
  if (!g_env_flag_names) {
    g_env_flag_names = new std::set<string>;
  }
  if (!g_env_flag_names->insert(name).second) {
    fprintf(stderr, "%s has registered twice\n", name);
    exit(1);
  }
}

void RegisterEnvAutoConfFlag(const char* name,
                             string (*GetConfiguredValue)(),
                             void (*SetConfiguredValue)()) {
  if (!g_autoconfigurers) {
    g_autoconfigurers = new AutoConfigurerMap;
  }

  GomaAutoConfigurer configurer(GetConfiguredValue, SetConfiguredValue);

  if (!g_autoconfigurers->insert(make_pair(string(name), configurer)).second) {
    fprintf(stderr, "%s has registered twice for autoconf\n", name);
    exit(1);
  }
}

void CheckFlagNames(const char** envp) {
  bool ok = true;
  for (int i = 0; envp[i]; i++) {
    if (strncmp(envp[i], "GOMA_", 5)) {
      continue;
    }
    const char* name_end = strchr(envp[i], '=');
    assert(name_end);
    const string name(envp[i] + 5, name_end - envp[i] - 5);
    if (!g_env_flag_names->count(name)) {
      fprintf(stderr, "%s: unknown GOMA_ parameter\n", envp[i]);
      ok = false;
    }
  }
  if (!ok) {
    exit(1);
  }
}

void AutoConfigureFlags(const char** envp) {
  std::set<string> goma_set_params;

  for (int i = 0; envp[i]; i++) {
    if (strncmp(envp[i], "GOMA_", 5))
      continue;

    const char* name_end = strchr(envp[i], '=');
    assert(name_end);
    const string name(envp[i] + 5, name_end - envp[i] - 5);
    goma_set_params.insert(name);
  }

  for (const auto& it : *g_autoconfigurers) {
    if (goma_set_params.count(it.first))
      continue;
    it.second.SetConfiguredValue();
  }
}

void DumpEnvFlag(std::ostringstream* ss) {
  if (g_env_flag_names == nullptr)
    return;

  for (const auto& iter : *g_env_flag_names) {
    const string name = "GOMA_" + iter;
    char* v = nullptr;
#ifdef _WIN32
    _dupenv_s(&v, nullptr, name.c_str());
#else
    v = getenv(name.c_str());
#endif
    if (v != nullptr) {
      (*ss) << name << "=" << v << std::endl;
    } else if (g_autoconfigurers->count(iter)) {
      (*ss) << name << "="
            << g_autoconfigurers->find(iter)->second.GetConfiguredValue()
            << " (auto configured)" << std::endl;
    }
  }
}

#ifdef _WIN32
string GOMA_EnvToString(const char* envname, const char* dflt) {
  char* env;
  if (_dupenv_s(&env, nullptr, envname) == 0 && env != nullptr) {
    string value = env;
    free(env);
    return value;
  } else {
    return dflt;
  }
}

bool GOMA_EnvToBool(const char* envname, bool dflt) {
  char* env;
  if (_dupenv_s(&env, nullptr, envname) == 0 && env != nullptr) {
    bool value = (memchr("tTyY1\0", env[0], 6) != nullptr);
    free(env);
    return value;
  } else {
    return dflt;
  }
}

int GOMA_EnvToInt(const char* envname, int dflt) {
  char* env;
  if (_dupenv_s(&env, nullptr, envname) == 0 && env != nullptr) {
    int value = strtol(env, nullptr, 10);
    free(env);
    return value;
  } else {
    return dflt;
  }
}

#endif
