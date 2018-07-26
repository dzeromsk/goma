// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// We share these flags among gomacc and compiler_proxy.
// As gomacc may start compiler_proxy, gomacc should accept flags for
// compiler_proxy and vice versa.

#include <algorithm>

#include "env_flags.h"
#include "machine_info.h"

#ifdef GOMA_DECLARE_FLAGS_ONLY
# undef GOMA_DEFINE_VARIABLE
# define GOMA_DEFINE_VARIABLE(type, name, value, meaning, tn)  \
    GOMA_DECLARE_VARIABLE(type, name, tn)
# undef GOMA_DEFINE_string
# define GOMA_DEFINE_string(name, value, meaning) GOMA_DECLARE_string(name)
# undef GOMA_REGISTER_AUTOCONF_FLAG_NAME
# define GOMA_REGISTER_AUTOCONF_FLAG_NAME(name, func)
#endif

#ifndef GOMA_DECLARE_FLAGS_ONLY
// TODO: We would like to know what the best number is?
static int NumDefaultProxyThreads() {
  int num_cpus = devtools_goma::GetNumCPUs();
  if (num_cpus > 0)
    return std::max(num_cpus, 2);

  return 16;
}

static int NumDefaultProxyHttpThreads() {
  int num_cpus = devtools_goma::GetNumCPUs();
#ifndef _WIN32
  const int kDivider = 4;
#else
  // Windows uses select for accepting sockets and FD_SETSIZE=64.
  // Limiting the number of threads limits not only usage of memory and cpu
  // but also limits the number of sockets to handle IPC.
  // Since it causes connection issues from gomacc under heavy gomacc usage,
  // we relaxes limitation of the number of IPC threads on Windows.
  // See: https://code.google.com/p/chromium/issues/detail?id=390764
  const int kDivider = 1;
#endif
  if (num_cpus > 0)
    return std::max(num_cpus / kDivider, 1);

  return 4;
}

// The max size of include cache.
// On Win or Mac, this will improve compile performance.
// On Linux, IncludeCache itself does not improve compile performance so much,
// however, IncludeCache is required to enable DepsCache.
// As of 2017, 32MB is not enough to cache all include headers if clobber
// build happens. So, in 64bit system, use 64MB by default.
static int MaxIncludeCacheSize() {
#if defined(__LP64__) || defined(_WIN64)
  int64_t memory_size = devtools_goma::GetSystemTotalMemory();
  int64_t gb = memory_size / 1024 / 1024 / 1024;
  if (gb >= 15)
    return 64;
#endif
  return 32;
}

static int MaxSubProcsLow() {
  int cpus = devtools_goma::GetNumCPUs();
  if (cpus > 0)
    return std::max(1, cpus / 5);
  return 1;
}

static int MaxSubProcs() {
  // +1 is for high priority task like local fallback.
  return MaxSubProcsLow() + 1;
}

static int MaxBurstSubProcs() {
  int cpus = devtools_goma::GetNumCPUs();
  if (cpus > 0)
    return 2 * cpus;
  return 6;
}

static int MaxBurstSubProcsHeavy() {
  int cpus = devtools_goma::GetNumCPUs();
  if (cpus >= 2)
    return cpus / 2;
  return 1;
}

#endif  // GOMA_DECLARE_FLAGS_ONLY

// For gomacc

GOMA_DEFINE_bool(DUMP, false, "Dump bunch of info");
GOMA_DEFINE_bool(DUMP_REQUEST, false, "Dump request protocol buffer");
GOMA_DEFINE_bool(DUMP_RESPONSE, false, "Dump response protocol buffer");
GOMA_DEFINE_bool(DUMP_TIME, false, "Dump time info");
GOMA_DEFINE_bool(DUMP_ARGV, false, "Dump arguments");
GOMA_DEFINE_bool(DUMP_APPENDLOG, false,
                 "Dump arguments to /tmp/fallback_command");
GOMA_DEFINE_bool(STORE_ONLY, false, "Don't use the shared cache in cloud");
GOMA_DEFINE_bool(USE_SUCCESS, false,
                 "Lookup the shared cache and store it only if it succeeded");
GOMA_DEFINE_bool(RETRY, true, "Retry when something failed");
GOMA_DEFINE_bool(FALLBACK, true, "Fallback when remote execution failed."
                 "Even it is false, compiler proxy will run local process "
                 "for non-compile command. "
                 "If false, implies GOMA_USE_LOCAL=false.");
GOMA_DEFINE_bool(USE_LOCAL, true, "Use local process when idle.");
GOMA_DEFINE_string(VERIFY_COMMAND, "",
                   "Verify command matches with backend."
                   "\"version\" will check by version of command."
                   "\"checksum\" will check by checksums of command and "
                   "subprograms."
                   "\"all\" will check all of above.");
GOMA_DEFINE_bool(VERIFY_OUTPUT, false,
                 "Verify output file with local compiler.");
GOMA_DEFINE_bool(VERIFY_ASSEMBLER_CODE, false,
                 "Verify assembler code with local compiler.");
GOMA_DEFINE_bool(VERIFY_PREPROCESS_CODE, false,
                 "Verify preprocessed code with local compiler.");
GOMA_DEFINE_string(COMPILER_PROXY_SOCKET_NAME, "goma.ipc",
                   "The unix domain socket name of the compiler proxy. "
                   "On Windows, this is named pipe's name.");
GOMA_DEFINE_int32(EXCLUSIVE_NUM_PROCS, 4,
                  "Max number of process to run simultaneously in fallback.");
GOMA_DEFINE_string(COMPILER_PROXY_BINARY, "compiler_proxy",
                   "Path to compiler_proxy binary");
GOMA_DEFINE_bool(OUTPUT_EXEC_RESP, false,
                 "Always outputs ExecResp");
GOMA_DEFINE_string(FALLBACK_INPUT_FILES, "",
                   "Comma separated list of files for which we use "
                   "local compilers (e.g., conftest.c,_configtest.c).");
GOMA_DEFINE_bool(FALLBACK_CONFTEST, true,
                 "Force local fallback for conftest source.");
GOMA_DEFINE_string(IMPLICIT_INPUT_FILES, "",
                   "Comma separated list of files to send to goma.");
#ifdef _WIN32
// devenv or msbuild would run cl.exe with multiple inputs.
// gomacc emits ExecReq per input file.
GOMA_DEFINE_bool(FAN_OUT_EXEC_REQ,  true,
                 "If true, gomacc do the fan-out compile request per "
                 "input filenames. "
                 "In this mode, verify flags are disabled.");
#endif

GOMA_DEFINE_bool(START_COMPILER_PROXY, false,
                 "If true, start compiler proxy when gomacc cannot find it.");
#ifndef _WIN32
GOMA_DEFINE_string(GOMACC_LOCK_FILENAME, "gomacc.lock",
                   "Filename to lock only single instance of compiler proxy "
                   "can startup.");
#else
GOMA_DEFINE_string(GOMACC_LOCK_GLOBALNAME,
                   "Global\\goma_cc_lock_compiler_proxy",
                   "Global mutex so that only one instance of compiler proxy "
                   "can startup.");
#endif
GOMA_DEFINE_int32(GOMACC_COMPILER_PROXY_RESTART_DELAY, 60,
                  "How long gomacc should wait before retrying to start the "
                  "compiler proxy.  This must be specified in sec.");
GOMA_DEFINE_bool(EXTERNAL_USER, false, "Send as an external user.");
GOMA_DEFINE_bool(DISABLED, false,
                 "Execute any commands locally without goma.  No throttling.  "
                 "Using with large -j option of ninja or make may be harmful.");
#ifdef __linux__
GOMA_DEFINE_string(LOAD_AVERAGE_LIMIT, "10",
                  "gomacc invokes a child process only when the load average "
                  "is below this value.  Will not wait if the value < 1.0");
GOMA_DEFINE_int32(MAX_SLEEP_TIME, 60,
                  "gomacc checks load average less than this time interval "
                  " (in sec) and sleeps between checks if load average is "
                  "higher than GOMA_LOAD_AVERAGE_LIMIT. "
                  "Will not wait if the value <= 0.");
#endif

#ifdef _WIN32
// 30 seconds default timeout is mitigation for b/36493466, b/70640154.
GOMA_DEFINE_int32(NAMEDPIPE_WAIT_TIMEOUT_MS, 30000,
                  "Timeout(in milliseconds) to wait in ConnectNamedpipe.");
#endif

GOMA_DEFINE_bool(GOMACC_ENABLE_CRASH_DUMP, false,
                 "True to store breakpad crash dump on gomacc.");
GOMA_DEFINE_bool(GOMACC_WRITE_LOG_FOR_TESTING, false,
                 "True to write log via glog.  Only for testing.");

// For compiler_proxy

GOMA_DEFINE_string(PROXY_HOST, "",
                   "The hostname of an HTTP proxy.");
GOMA_DEFINE_int32(PROXY_PORT, 0,
                  "The port of an HTTP proxy.");

#define DEFAULT_SETTINGS_SERVER "https://cxx-compiler-service.appspot.com/settings"
#define DEFAULT_STUBBY_PROXY_IP_ADDRESS ""
GOMA_DEFINE_string(SETTINGS_SERVER, DEFAULT_SETTINGS_SERVER,
                   "Settings server URL");
GOMA_DEFINE_string(ASSERT_SETTINGS,
                   "",
                   "Assert settings name matches with this value, "
                   "if specified.");
GOMA_DEFINE_string(STUBBY_PROXY_IP_ADDRESS, DEFAULT_STUBBY_PROXY_IP_ADDRESS,
                   "The IP address or hostname of the goma server");
GOMA_DEFINE_int32(STUBBY_PROXY_PORT, 443,
                  "The port of the stubby proxy, or GFE.");
GOMA_DEFINE_string(URL_PATH_PREFIX, "/cxx-compiler-service",
                   "The HTTP RPC URL path prefix.");
GOMA_DEFINE_string(COMPILER_PROXY_LISTEN_ADDR, "localhost",
                   "The address that compiler proxy listens for http."
                   "INADDR_LOOPBACK(127.0.0.1) for 'localhost'"
                   "INADDR_ANY for ''.");
GOMA_DEFINE_int32(COMPILER_PROXY_PORT, 8088,
                  "The port of the compiler proxy.");

GOMA_DEFINE_bool(COMPILER_PROXY_REUSE_CONNECTION, true,
                 "Connection is reused for multiple rpcs.");

// See  http://smallvoid.com/article/winnt-tcpip-max-limit.html
// Remember to read the comments by the author.  For Vista/Win7 (where goma is
// targeted at), the max number of sockets is the number of ports available for
// establishing connections, which is, 65535 - 1.
GOMA_DEFINE_int32(COMPILER_PROXY_MAX_SOCKETS, 65534,
                  "Maximum connections supported on Windows.");
GOMA_DEFINE_int32(COMPILER_PROXY_NUM_FIND_PORTS, 10,
                  "Compiler proxy searches a free port by incrementing "
                  "the port number at most this value when the port "
                  "specified by COMPILER_PROXY_PORT is in use.");
GOMA_DEFINE_AUTOCONF_int32(COMPILER_PROXY_THREADS, NumDefaultProxyThreads,
                           "Number of threads compiler proxy will run in.");
GOMA_DEFINE_AUTOCONF_int32(COMPILER_PROXY_HTTP_THREADS,
                           NumDefaultProxyHttpThreads,
                           "Number of threads compiler proxy will handle "
                           "http/ipc request.");
GOMA_DEFINE_AUTOCONF_int32(INCLUDE_PROCESSOR_THREADS, NumDefaultProxyThreads,
                           "Number of threads for include processor.");
#ifdef _WIN32
#define DEFAULT_MAX_OVERCOMIT_INCOMING_SOCKETS 64
#else
#define DEFAULT_MAX_OVERCOMIT_INCOMING_SOCKETS 0
#endif
GOMA_DEFINE_int32(MAX_OVERCOMMIT_INCOMING_SOCKETS,
                  DEFAULT_MAX_OVERCOMIT_INCOMING_SOCKETS,
                  "Number of overcommitted incoming sockets per threads on "
                  "select.");
#if defined(__LP64__) || defined(_WIN64)
# define DEFAULT_MAX_ACTIVE_TASKS 2048
#else  // 32bit system would not have enough memory
# define DEFAULT_MAX_ACTIVE_TASKS 200
#endif
GOMA_DEFINE_int32(MAX_ACTIVE_TASKS, DEFAULT_MAX_ACTIVE_TASKS,
                  "Number of active tasks.");
GOMA_DEFINE_int32(MAX_FINISHED_TASKS, 1024,
                  "Number of task information to keep for monitoring.");
GOMA_DEFINE_int32(MAX_FAILED_TASKS, 1024,
                  "Number of failed task information to keep for monitoring.");
GOMA_DEFINE_int32(MAX_LONG_TASKS, 50,
                  "Number of long taks information to keep for monitoring.");
GOMA_DEFINE_bool(COMPILER_PROXY_STORE_FILE, false,
                 "True to store files first.  False to believe FileService "
                 "already has files and not send new file content.");
GOMA_DEFINE_int32(COMPILER_PROXY_NEW_FILE_THRESHOLD, 60 * 60,
                  "Time(sec) to consider new file if the file is modified "
                  "in that time.");
GOMA_DEFINE_int32(PING_TIMEOUT_SEC, 60,
                  "Time(sec) for initial ping timeout.");
GOMA_DEFINE_int32(PING_RETRY_INTERVAL, 10,
                  "Time(sec) interval for retrying initial ping.");
GOMA_DEFINE_string(COMPILER_PROXY_RPC_TIMEOUT_SECS, "610",
                   "Time(sec) for HttpRPC timeouts.");
GOMA_DEFINE_string(COMMAND_CHECK_LEVEL, "",
                   "Level of command equivalence. "
                   "Default (\"\") will check by command name and target "
                   "architecture. "
                   "\"version\" will check by version of command. "
                   "\"checksumn\" will check by checksum of command.");
GOMA_DEFINE_string(HERMETIC, "fallback",
                   "Hermetic mode: one of \"off\", \"fallback\" or \"error\". "
                   "If it is not \"off\", use the compiler with the same "
                   "version string and binary hash in backend.  If no such "
                   "compiler is found, run local compiler (for \"fallback\") "
                   "or response error (for \"error\") and never try "
                   "sending request again for the same compiler. "
                   "This flag will override GOMA_FALLBACK when hermetic "
                   "compiler is not found.");
GOMA_DEFINE_int32(LOCAL_RUN_PREFERENCE, 3,
                  "Local run preference. "
                  "If local process has started before this stage of goma's "
                  "process (e.g. CompileTask::State), stop racing and "
                  "ignore goma. ");
GOMA_DEFINE_bool(LOCAL_RUN_FOR_FAILED_INPUT, true,
                 "Prefer local run for previous failed input filename. ");
GOMA_DEFINE_int32(LOCAL_RUN_DELAY_MSEC, 0,
                  "msec to delay for idle fallback.");
GOMA_DEFINE_AUTOCONF_int32(
    MAX_SUBPROCS,
    MaxSubProcs,
    "Maximum number of subprocesses that run at the same time.");
GOMA_DEFINE_AUTOCONF_int32(MAX_SUBPROCS_LOW,
                           MaxSubProcsLow,
                           "Maximum number of subprocesses with low priority "
                           "(e.g. compile locally while requesting to goma). "
                           "fallback process gets high priority.");
GOMA_DEFINE_int32(MAX_SUBPROCS_HEAVY, 1,
                  "Maximum number of subprocesses with heavy weight "
                  "(such as link).");
GOMA_DEFINE_AUTOCONF_int32(COMPILER_INFO_POOL,
                           MaxSubProcs,
                           "Maximum number of subprocesses for compiler info "
                           "that run at the same time.");
GOMA_DEFINE_AUTOCONF_int32(BURST_MAX_SUBPROCS, MaxBurstSubProcs,
                           "Maximum number of subprocesses when remote server "
                           "is not available. When remote server is not "
                           "available, goma tries to use local cpu more.");
GOMA_DEFINE_AUTOCONF_int32(BURST_MAX_SUBPROCS_LOW, MaxBurstSubProcs,
                           "Maximum number of subprocesses with low priority "
                           "when remote server is not available. In most cases "
                           "local compile is low priority. So it is "
                           "recommended to set this the same number as "
                           "BURST_MAX_SUBPROCS.");
GOMA_DEFINE_AUTOCONF_int32(BURST_MAX_SUBPROCS_HEAVY, MaxBurstSubProcsHeavy,
                           "Maximum number of subprocesses with heavy weight "
                           "when remote server is not available.");
GOMA_DEFINE_int32(MAX_SUBPROCS_PENDING, 3,
                  "Threshold to prefer local run to remote goma.");
// TODO: autoconf
GOMA_DEFINE_int32(MAX_SUM_OUTPUT_SIZE_IN_MB, 64,
                  "The max size for output buffer in MB.");
GOMA_DEFINE_bool(STORE_LOCAL_RUN_OUTPUT, false,
                 "Store local run output in goma cache.");
GOMA_DEFINE_bool(ENABLE_REMOTE_LINK, false, "Enable remote link.");
GOMA_DEFINE_bool(USE_RELATIVE_PATHS_IN_ARGV, false,
                 "Use relative paths in argv, except system directories.");
GOMA_DEFINE_string(TMP_DIR, "",
                   "Temporary Directory.  Ignored on Windows.");
GOMA_DEFINE_string(CACHE_DIR, "",
                   "A directory to store goma's cache data. e.g. CRLs");
GOMA_DEFINE_string(COMPILER_PROXY_LOCK_FILENAME, "goma_compiler_proxy.lock",
                   "Filename to lock only single instance of compiler proxy "
                   "can startup.");
GOMA_DEFINE_string(RPC_EXTRA_PARAMS, "",
                   "Extra parameter to append to RPC path.");

GOMA_DEFINE_int32(MULTI_STORE_IN_CALL, 128,
                  "Number of FileBlob in StoreFileReq");
GOMA_DEFINE_int32(MULTI_STORE_THRESHOLD_SIZE_IN_CALL, 12 * 1024 * 1024,
                  "Threshold size to issue StoreFileReq");
GOMA_DEFINE_int32(MULTI_STORE_PENDING_MS, 100,
                  "Pending time in ms to issue StoreFileReq.");
GOMA_DEFINE_int32(NUM_LOG_IN_SAVE_LOG, 512,
                  "Number of ExecLog in SaveLogReq");
GOMA_DEFINE_int32(LOG_PENDING_MS, 30 * 1000,
                  "Pending time in ms to save log.");
// See RFC1918 for private address space.
GOMA_DEFINE_string(COMPILER_PROXY_TRUSTED_IPS,
                   "127.0.0.1,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16",
                   "Trusted IP networks that are allowed to access "
                   "compiler proxy status page. "
                   "By default, localhost and private address spaces are "
                   "considered as trusted.");
GOMA_DEFINE_bool(ENABLE_GCH_HACK, false,
                 "Enable *.gch hack");
GOMA_DEFINE_AUTOCONF_int32(MAX_INCLUDE_CACHE_SIZE,
                           MaxIncludeCacheSize,
                           "The size of include cache in MB.");
GOMA_DEFINE_int32(MAX_LIST_DIR_CACHE_ENTRY_NUM, 32768,
                  "The entry limit in list dir cache.");
GOMA_DEFINE_string(CONTENT_TYPE_FOR_PROTOBUF, "binary/x-protocol-buffer",
                   "Content-Type for goma's HttpRPC requests.");
GOMA_DEFINE_bool(BACKEND_SOFT_STICKINESS, false,
                 "Enable backend soft stickiness, i.e. set cookie header.");
GOMA_DEFINE_bool(BACKEND_SOFT_STICKINESS_REFRESH, true,
                 "Use randomly created cookie for backend soft stickiness.");
GOMA_DEFINE_string(HTTP_AUTHORIZATION_FILE, "",
                   "Debug only. File that stores Authorization header, "
                   "if it is not empty.");
GOMA_DEFINE_string(OAUTH2_CONFIG_FILE, "",
                   "File that stores configs on OAuth2."
                   "The file is JSON-like format, and client_id, client_secret,"
                   " and refresh_token should be set. "
#ifndef _WIN32
                   "$HOME/.goma_oauth2_config"
#else
                   "%USERPROFILE%\\.goma_oauth2_config"
#endif
                   " will be used if no other auth config set.");
GOMA_DEFINE_string(GCE_SERVICE_ACCOUNT, "",
                   "service account name in Google Compute Engine.");
GOMA_DEFINE_string(SERVICE_ACCOUNT_JSON_FILE, "",
                   "File that stores service account json, downloaded from "
                   "google cloud console."
                   "It will be read everytime when access token need to be "
                   "refreshed. It should be absolute path.");
GOMA_DEFINE_string(HTTP_HOST, "",
                   "Alternative host name shown in HTTP Host field. "
                   "If you use SSL tunnel, compiler proxy connects localhost "
                   "but the expected Host field might not be localhost.");
GOMA_DEFINE_bool(USE_SSL, true,
                 "Communicate server with SSL.");
GOMA_DEFINE_string(SSL_EXTRA_CERT, "",
                   "Path to an additional SSL certificate file (PEM) for "
                   "communication to goma server, not used for oauth2 token "
                   "exchanges."
                   "We automatically load our default certificate.");
GOMA_DEFINE_string(SSL_EXTRA_CERT_DATA, "",
                   "An additional SSL certificate (PEM) for "
                   "communication to goma server, not used for oauth2 token "
                   "exchanges.");
GOMA_DEFINE_int32(SSL_CRL_MAX_VALID_DURATION, -1,
                  "Max valid duration of CRL from CRL's lastUpdafe field in "
                  "seconds. "
                  "We caches downloaded CRLs no more than this duration. "
                  "If negative, compiler_proxy follows nextUpdate in CRL.");
#define DEFAULT_PROVIDE_INFO false
#define DEFAULT_SEND_USER_INFO false

GOMA_DEFINE_bool(PROVIDE_INFO, DEFAULT_PROVIDE_INFO,
                 "Provide info. to Google for improving the service. "
                 "If enabled, compiler proxy sends timing stats and parameters "
                 "for both remote tasks and local tasks. It also sends "
                 "username and nodename if GOMA_SEND_USER_INFO is true.");
GOMA_DEFINE_bool(SEND_USER_INFO, DEFAULT_SEND_USER_INFO,
                 "Send username and nodename with each request."
                 "If false, it will use anonymized user info for "
                 "compiler_proxy_id.");
GOMA_DEFINE_string(USE_CASE, "",
                   "goma use case name. It is used for choosing GCE goma "
                   "backend settings.");
GOMA_DEFINE_string(DEPS_CACHE_FILE, "",
                   "Path to the DepsCache cache file. It eliminates "
                   "unnecessary preprocess to improve the goma performance. "
                   "If empty, deps cache won't be used. "
                   "If not absolute path, it will be in GOMA_CACHE_DIR.");
GOMA_DEFINE_int32(DEPS_CACHE_IDENTIFIER_ALIVE_DURATION, 3 * 24 * 3600,
                  "Deps cache older than this value (in second) will be "
                  "removed in saving/loading. If negative, any cache won't be "
                  "removed.");
GOMA_DEFINE_int32(DEPS_CACHE_TABLE_THRESHOLD, 70000,
                  "The max size of DepsCache table threshold. If the number of "
                  "DepsCache table exceeds this value, older DepsCache entry "
                  "will be removed in saving.");
GOMA_DEFINE_int32(DEPS_CACHE_MAX_PROTO_SIZE_IN_MB, 128,
                  "The max size of DepsCache file. If the file size exceeds "
                  "this limit, loading will fail. Unit is MB.");
GOMA_DEFINE_string(COMPILER_INFO_CACHE_FILE, "compiler_info_cache",
                   "Filename of compiler_info's cache. "
                   "If empty, compiler_info cache file is not used. "
                   "If not absolute path, it will be in GOMA_CACHE_DIR.");
GOMA_DEFINE_bool(ENABLE_GLOBAL_FILE_ID_CACHE,
                 false,
                 "(Deprecated) Use ENABLE_GLOBAL_FILE_STAT_CACHE instead. "
                 "Enable global file stat cache. "
                 "Do not enable this flag when any source file would be "
                 "changed between compilations.");
GOMA_DEFINE_bool(ENABLE_GLOBAL_FILE_STAT_CACHE,
                 false,
                 "Enable global file stat cache. "
                 "Do not enable this flag when any source file would be "
                 "changed between compilations.");
GOMA_DEFINE_int32(COMPILER_INFO_CACHE_HOLDING_TIME_SEC, 60 * 60 * 24 * 30,
                  "CompilerInfo is not evicted if it is used within "
                  "COMPILER_INFO_CACHE_HOLDING_TIME_SEC. "
                  "Otherwise it is evicted when it is loaded from file.");
GOMA_DEFINE_string(DUMP_STATS_FILE, "",
                   "Filename to dump stats at the end of compiler_proxy."
                   "If empty, nothing will be dumped.");
#ifdef HAVE_COUNTERZ
GOMA_DEFINE_string(DUMP_COUNTERZ_FILE, "",
                   "Filename to dump counterz stat at the end of "
                   "compiler_proxy. If empty, nothing will be dumped. "
                   "If it endswith \".json\", stats is exported in json "
                   "format, otherwise exported in binary protobuf.");
GOMA_DEFINE_bool(ENABLE_COUNTERZ, false,
                 "Flag for profiling using counterz.");
#endif
GOMA_DEFINE_string(HASH_REWRITE_RULE_FILE, "",
                   "(DEPRECATED)");
GOMA_DEFINE_string(LOCAL_OUTPUT_CACHE_DIR, "",
                   "Directory that LocalOutputCache uses");
GOMA_DEFINE_int32(LOCAL_OUTPUT_CACHE_MAX_CACHE_AMOUNT_IN_MB, 1024*30,
                  "The max size of local output cache. If the total amount "
                  "exceeds this, older cache will be removed.");
GOMA_DEFINE_int32(LOCAL_OUTPUT_CACHE_THRESHOLD_CACHE_AMOUNT_IN_MB, 1024*20,
                  "When LocalOutputCache garbage collection run, entries will "
                  "be removed until total size is below this value.");
GOMA_DEFINE_int32(LOCAL_OUTPUT_CACHE_MAX_ITEMS, 100000,
                  "The max number of cache items. If exceeds this item, older "
                  "cache will be removed");
GOMA_DEFINE_int32(LOCAL_OUTPUT_CACHE_THRESHOLD_ITEMS, 80000,
                  "When LocalOutputCache garbage collection run, entries will "
                  "be removed until the number of entries are below of this "
                  "value");

#ifdef _WIN32
#define DEFAULT_CTL_SCRIPT_NAME "goma_ctl.bat"
#else
#define DEFAULT_CTL_SCRIPT_NAME "goma_ctl.py"
#endif
GOMA_DEFINE_string(CTL_SCRIPT_NAME,
                   DEFAULT_CTL_SCRIPT_NAME,
                   "File name of goma control script. This is used for pulling "
                   "latest updates in idle time and usually automatically "
                   "set by the script itself. You SHOULD NOT set this value "
                   "manually unless you know what you are doing.");
GOMA_DEFINE_bool(COMPILER_PROXY_ENABLE_CRASH_DUMP, false,
                 "True to store breakpad crash dump on compiler_proxy.");

// We keep this flag to provide future workarounds for subproc bugs.
//
// clang left crash report scripts and data if only clang's child process
// (not driver process) killed.
// https://bugs.chromium.org/p/chromium/issues/detail?id=668548
// goma failed to kill nacl compiler process? goma killed wrapper script,
// but it failed to wait for nacl compiler?
// https://bugs.chromium.org/p/chromium/issues/detail?id=668497
//
// On old MacOSX, a process happens to enter uninterruptible sleep state ('U'),
// and it hangs to kill such process. b/5266411
// jam@ confirmed the recent MacOSX does not affected by this bug.
// https://code.google.com/p/chromium/issues/detail?id=387934
// TODO: enable this again. (b/80404226, b/111241394#comment6)
GOMA_DEFINE_bool(DONT_KILL_SUBPROCESS,
                 true,
                 "Don't kill subprocess.");

#ifdef _WIN32
# define DEFAULT_DONT_KILL_COMMANDS \
    "x86_64-nacl-gcc,x86_64-nacl-g++,i686-nacl-gcc,i686-nacl-g++," \
    "pnacl-clang,pnacl-clang++"
#else
# define DEFAULT_DONT_KILL_COMMANDS ""
#endif
GOMA_DEFINE_string(DONT_KILL_COMMANDS,
                   DEFAULT_DONT_KILL_COMMANDS,
                   "Don't kill commands. "
                   "On Windows, nacl-gcc sometimes remains its child process "
                   "with suspended state.  In that situation, \"goma_ctl.bat "
                   "start\" cannot start compiler_proxy. "
                   "b/13198323 b/12533849");

#ifndef _WIN32
GOMA_DEFINE_bool(COMPILER_PROXY_DAEMON_MODE, false,
                 "True to run as a daemon process.");
GOMA_DEFINE_string(COMPILER_PROXY_DAEMON_STDERR, "goma_compiler_proxy.stderr",
                   "Where to write stderr output when running in daemon mode. "
                   "Used only when COMPILER_PROXY_DAEMON_MODE is true.");
#endif

GOMA_DEFINE_bool(ENABLE_AUTO_UPDATE, true, "Enable auto updater.");
GOMA_DEFINE_int32(AUTO_UPDATE_IDLE_COUNT, 4 * 60 * 60,
                  "Try to update to the latest version if compiler_proxy "
                  "has been idle for approx this number of seconds.");

GOMA_DEFINE_int32(WATCHDOG_TIMER, 4 * 60 * 60,
                  "Watchdog timer in seconds."
                  "Watchdog is disabled if this value is not positive.");

GOMA_DEFINE_int32(LOG_CLEAN_INTERVAL, 24 * 60 * 60,
                  "Interval seconds to clean old logs.");

GOMA_DEFINE_int32(MEMORY_TRACK_INTERVAL, 60,
                  "Interval seconds to track compiler_proxy memory. "
                  "Periodical memory tracking is disabled if this value is not "
                  "positive.");
#if defined(__LP64__) || defined(_WIN64)
// 4GB on 64bit
# define DEFAULT_MEMORY_WARNING_THRESHOLD_IN_MB (1024 * 4)
#else
// 1.5GB on 32bit
# define DEFAULT_MEMORY_WARNING_THRESHOLD_IN_MB (1024 + 512)
#endif
GOMA_DEFINE_int32(MEMORY_WARNING_THRESHOLD_IN_MB,
                  DEFAULT_MEMORY_WARNING_THRESHOLD_IN_MB,
                  "If consuming memory exceeds this value, warning log will be"
                  " shown.");

GOMA_DEFINE_bool(ENABLE_CONTENTIONZ, true, "Enable contentionz");

GOMA_DEFINE_int32(ALLOWED_NETWORK_ERROR_DURATION, -1,
                  "Compiler_proxy will make compile error after this duration "
                  "(in seconds) from when network error has been started. "
                  "This feature is disabled if negative value is set.");
GOMA_DEFINE_int32(NETWORK_ERROR_THRESHOLD_PERCENT, -1,
                  "HTTP client in compiler_proxy consider network is "
                  "unhealthy if non-200 HTTP response comes more than this "
                  "percentage."
                  "Use the default value if negative value is given.");

GOMA_DEFINE_bool(FAIL_FAST, false,
                 "fail fast mode of compiler proxy.");

GOMA_DEFINE_bool(FAIL_FOR_UNSUPPORTED_COMPILER_FLAGS, true,
                 "compile fails if a compile request is rejected by goma "
                 "server and the rejection reason is the compile request "
                 "contains unsupoprted compiler flags.");

GOMA_DEFINE_int32(MAX_ACTIVE_FAIL_FALLBACK_TASKS, -1,
                  "Compiler_proxy will make compile error without trying local "
                  "fallback if the number of local fallbacks by remote compile "
                  "failure gets larger than this value and go over the allowed "
                  "duration set by ALLOWED_MAX_ACTIVE_FAIL_FALLBACK_DURATION. "
                  "This feature is disabled if negative value is set.");
GOMA_DEFINE_int32(ALLOWED_MAX_ACTIVE_FAIL_FALLBACK_DURATION, -1,
                  "Compiler_proxy will make compile error if the number of "
                  "local fallbacks by remote compile failure gets larger than "
                  "MAX_ACTIVE_FAIL_FALLBACK_TASKS and reaches this duration "
                  "(in seconds). value <= 0 means duration is set to 0.");

GOMA_DEFINE_int32(MAX_COMPILER_DISABLED_TASKS, -1,
                  "Compiler_proxy will enter burst mode if the number of "
                  "setup failure caused by compiler disabled gets larger than "
                  "this value.  This feature is disabled if negative value "
                  "is set.");

#if HAVE_HEAP_PROFILER
GOMA_DEFINE_string(COMPILER_PROXY_HEAP_PROFILE_FILE, "goma_compiler_proxy_heapz",
                   "heap profile filename.");
#endif
#if HAVE_CPU_PROFILER
GOMA_DEFINE_string(COMPILER_PROXY_CPU_PROFILE_FILE,
                   "goma_compiler_proxy_profilez",
                   "cpu profile filename.");
GOMA_DEFINE_string(INCLUDE_PROCESSOR_CPU_PROFILE_FILE,
                   "goma_include_processor_profilez",
                   "cpu profile filename.");
#endif

// HTTP RPC
// Data for a big (>6MB) real data:
//
// 1: 6358280->1406854 in 101ms
// 2: 6358280->1331872 in 108ms
// 3: 6358280->1278628 in 118ms
// 4: 6358280->1171559 in 152ms
// 5: 6358280->1116972 in 186ms
// 6: 6358280->1090649 in 236ms
// 7: 6358280->1086307 in 261ms
// 8: 6358280->1082820 in 366ms
// 9: 6358280->1082162 in 459ms
//
// It seems somewhere from 3 to 6 would be a nice value.
GOMA_DEFINE_int32(HTTP_RPC_COMPRESSION_LEVEL, 3,
                  "Compression level in HttpRPC [0..9]."
                  "0 forces to disable compression.");
GOMA_DEFINE_string(HTTP_ACCEPT_ENCODING, "deflate",
                   "Accept-Encoding of goma's requests (e.g., lzma2)");
GOMA_DEFINE_bool(HTTP_RPC_START_COMPRESSION, true,
                 "Starts with compressed request. "
                 "Compression will be enabled/disabled by Accept-Encoding "
                 "in server's response.");
GOMA_DEFINE_bool(HTTP_RPC_CAPTURE_RESPONSE_HEADER, false,
                 "Capture every response header."
                 "By default, it only captures response header of "
                 "http error.");
GOMA_DEFINE_string(HTTP_SOCKET_READ_TIMEOUT_SECS, "1.0",
                   "Time(sec) for once the socket receives response header.");

GOMA_DEFINE_int32(HTTP_RPC_MIN_RETRY_BACKOFF, 500,
                  "Minimum Time(millesec) for retry backoff for HttpRPC. "
                  "Backoff time is randomized by subtracing 40%, so actual "
                  "minimum backoff time would be 60% of this value.");
GOMA_DEFINE_int32(HTTP_RPC_MAX_RETRY_BACKOFF, 5000,
                  "Minimum Time(millesec) for retry backoff for HttpRPC.");

GOMA_DEFINE_int32(RPC_TRACE_PERIOD, 0,
                  "How often to request RPC traces on the server. Traces will "
                  "be requested every nth request (i.e. 0 means never, 1 means "
                  "always, 10 means every 10th request)");

GOMA_DEFINE_string(API_TASKZ_FILE_FOR_TEST, "",
                   "Show the content of this file in /api/taskz. "
                   "For testing only.");

// For goma_fetch
GOMA_DEFINE_int32(FETCH_RETRY, 5,
                  "Times to retry for 50x error in http get");

// script or wrapper
GOMA_DEFINE_string(
    DIR, "",
    "Not used by this program, but may be set by wrapper scripts.");
