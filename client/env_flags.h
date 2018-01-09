// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_ENV_FLAGS_H_
#define DEVTOOLS_GOMA_CLIENT_ENV_FLAGS_H_

#include <stdlib.h>
#include <string.h>

#include <string>
#include <sstream>

void RegisterEnvFlag(const char* name);
void RegisterEnvAutoConfFlag(const char* name,
                             std::string (*GetConfiguredValue)(),
                             void (*SetConfiguredValue)());
void CheckFlagNames(const char** envp);
void AutoConfigureFlags(const char** envp);
void DumpEnvFlag(std::ostringstream* ss);

#ifdef _WIN32
// MSVS warns the usage of 'getenv'.
std::string GOMA_EnvToString(const char* envname, const char* dflt);
bool GOMA_EnvToBool(const char* envname, bool dflt);
int GOMA_EnvToInt(const char* envname, int dflt);

#else
// These macros (could be functions, but I don't want to bother with a .cc
// file), make it easier to initialize flags from the environment.

#define GOMA_EnvToString(envname, dflt)         \
  (!getenv(envname) ? (dflt) : getenv(envname))

#define GOMA_EnvToBool(envname, dflt)                                   \
  (!getenv(envname) ? (dflt) : memchr("tTyY1\0", getenv(envname)[0], 6) != NULL)

#define GOMA_EnvToInt(envname, dflt)                                    \
  (!getenv(envname) ? (dflt) : strtol(getenv(envname), NULL, 10))
#endif

#define GOMA_REGISTER_FLAG_NAME(name)                                   \
  struct RegisterEnvFlag##name {                                        \
    explicit RegisterEnvFlag##name() {                                  \
      RegisterEnvFlag(#name);                                           \
    }                                                                   \
  };                                                                    \
  RegisterEnvFlag##name g_register_env_flag_##name

#define GOMA_REGISTER_AUTOCONF_FLAG_NAME(name, func)                    \
  struct RegisterEnvAutoConfFlagSetter##name {                          \
    static void SetConfiguredValue() {                                  \
      FLAGS_ ## name = func();                                          \
    }                                                                   \
    /* Since we would like to use this kind of method for all types */  \
    /* (e.g. int, bool, etc.), we chose to return string */             \
    static std::string GetConfiguredValue() {                           \
      std::ostringstream ss;                                             \
      ss << func();                                                     \
      return ss.str();                                                  \
    }                                                                   \
  };                                                                    \
  struct RegisterEnvAutoConfFlag##name {                                \
    RegisterEnvAutoConfFlag##name() {                                   \
      RegisterEnvAutoConfFlag(                                          \
          #name,                                                        \
          RegisterEnvAutoConfFlagSetter##name::GetConfiguredValue,      \
          RegisterEnvAutoConfFlagSetter##name::SetConfiguredValue);     \
    }                                                                   \
  };                                                                    \
  RegisterEnvAutoConfFlag##name g_register_autoconf_flag_##name;        \


#define GOMA_DECLARE_VARIABLE(type, name, tn)                           \
  namespace FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_##tn##_instead { \
  extern type FLAGS_##name;                                             \
  }                                                                     \
  using FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_##tn##_instead::FLAGS_##name
#define GOMA_DEFINE_VARIABLE(type, name, value, meaning, tn)            \
  namespace FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_##tn##_instead { \
  type FLAGS_##name(value);                                             \
  }                                                                     \
  using FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_##tn##_instead::FLAGS_##name;\
  GOMA_REGISTER_FLAG_NAME(name)

// bool specialization
#define GOMA_DECLARE_bool(name)                 \
  GOMA_DECLARE_VARIABLE(bool, name, bool)
#define GOMA_DEFINE_bool(name, value, meaning)                          \
  GOMA_DEFINE_VARIABLE(bool, name, GOMA_EnvToBool("GOMA_" #name, value), \
                       meaning, bool)

typedef int int32;

// int32 specialization
#define GOMA_DECLARE_int32(name)                \
  GOMA_DECLARE_VARIABLE(int32, name, int32)
#define GOMA_DEFINE_int32(name, value, meaning)                         \
  GOMA_DEFINE_VARIABLE(int32, name, GOMA_EnvToInt("GOMA_" #name, value), \
                       meaning, int32)
#define GOMA_DEFINE_AUTOCONF_int32(name, func, meaning) \
  GOMA_DEFINE_int32(name, 0, meaning); \
  GOMA_REGISTER_AUTOCONF_FLAG_NAME(name, func)

// Special case for string, because we have to specify the namespace
// std::string, which doesn't play nicely with our FLAG__namespace hackery.
#define GOMA_DECLARE_string(name)                                       \
  namespace FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_string_instead { \
    extern  std::string FLAGS_##name;                                   \
  }                                                                     \
  using FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_string_instead::FLAGS_##name

#define GOMA_DEFINE_string(name, value, meaning)                        \
  namespace FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_string_instead { \
  std::string FLAGS_##name(GOMA_EnvToString("GOMA_" #name, value));     \
  }                                                                     \
  using FLAG__namespace_do_not_use_directly_use_GOMA_DECLARE_string_instead::FLAGS_##name; \
  GOMA_REGISTER_FLAG_NAME(name)

#endif  // DEVTOOLS_GOMA_CLIENT_ENV_FLAGS_H_
