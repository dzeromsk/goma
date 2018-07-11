// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef _WIN32
#error "This is a Windows-only unit test"
#endif

#include "spawner_win.h"

#include "compiler_specific.h"
#include "path.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace devtools_goma {

TEST(SpawnerWin, SpawnerAndLogToFile) {
  devtools_goma::SubProcessReq req;

  char buffer[PATH_MAX] = {0};
  GetModuleFileNameA(nullptr, buffer, PATH_MAX);
  *strrchr(buffer, '\\') = 0;

  const string cwd(buffer);
  const string prog(".\\dump_env.exe");
  std::vector<string> argv;
  argv.push_back("dump_env.exe");
  argv.push_back("arg1");
  argv.push_back("arg2");
  std::vector<string> envs;
  envs.push_back("TEST_STRING1=goma");
  envs.push_back("TEST_STRING2=win");
  // TODO: remove these when spawn_win do not find command.
  envs.push_back("PATH=" + devtools_goma::GetEnv("PATH"));
  envs.push_back("PATHEXT=" + devtools_goma::GetEnv("PATHEXT"));

  const string stdout_filename("dump_env.stdout.log");
  const string stderr_filename("dump_env.stderr.log");

  // priority not supported yet
  // req.set_priority(devtools_goma::SubProcessReq_Priority_HIGH_PRIORITY);

  strcat_s(buffer, PATH_MAX, "\\");
  strcat_s(buffer, PATH_MAX, "dump_env.stdout.log");
  _unlink(buffer);

  devtools_goma::SpawnerWin spawner;
  spawner.SetFileRedirection("", stdout_filename, stderr_filename,
                             devtools_goma::Spawner::MERGE_STDOUT_STDERR);
  spawner.SetDetach(false);
  int pid = spawner.Run(prog, argv, envs, cwd);
  EXPECT_NE(0, pid);
  while (spawner.IsChildRunning())
    spawner.Wait(devtools_goma::Spawner::WAIT_INFINITE);

  FILE* fp = nullptr;
  fopen_s(&fp, buffer, "r");
  ASSERT_TRUE(fp != nullptr);
  char temp[PATH_MAX];
  fgets(temp, PATH_MAX, fp);  // first line is the exe name
  EXPECT_STREQ(".\\dump_env.exe\n", temp);

  fgets(temp, PATH_MAX, fp);
  EXPECT_STREQ("arg1\n", temp);
  fgets(temp, PATH_MAX, fp);
  EXPECT_STREQ("arg2\n", temp);

  // These envs are come from "cmd /c" prepending
  fgets(temp, PATH_MAX, fp);
  EXPECT_TRUE(absl::StartsWith(temp, "COMSPEC="));
  EXPECT_EQ("comspec=c:\\windows\\system32\\cmd.exe\n",
            absl::AsciiStrToLower(temp));

  fgets(temp, PATH_MAX, fp);
  EXPECT_STREQ("PROMPT=$P$G\n", temp);

  fgets(temp, PATH_MAX, fp);
  EXPECT_STREQ("TEST_STRING1=goma\n", temp);
  fgets(temp, PATH_MAX, fp);
  EXPECT_STREQ("TEST_STRING2=win\n", temp);

  fclose(fp);
}

TEST(SpawnerWin, SpawnerAndLogToString) {
  char buffer[PATH_MAX] = {0};
  GetModuleFileNameA(nullptr, buffer, PATH_MAX);
  *strrchr(buffer, '\\') = 0;

  std::string cwd(buffer);
  std::string prog(".\\dump_env.exe");
  std::vector<std::string> argv, env;
  argv.push_back("dump_env.exe");
  argv.push_back("arg1");
  argv.push_back("arg2");
  env.push_back("TEST_STRING1=goma");
  env.push_back("TEST_STRING2=win");
  // TODO: remove these when spawn_win do not find command.
  env.push_back("PATH=" + devtools_goma::GetEnv("PATH"));
  env.push_back("PATHEXT=" + devtools_goma::GetEnv("PATHEXT"));

  // priority not supported yet
  // req.set_priority(devtools_goma::SubProcessReq_Priority_HIGH_PRIORITY);

  devtools_goma::SpawnerWin spawner;
  std::string output;
  spawner.SetConsoleOutputBuffer(&output,
                                 devtools_goma::Spawner::MERGE_STDOUT_STDERR);
  int pid = spawner.Run(prog, argv, env, cwd);
  EXPECT_NE(0, pid);
  while (spawner.IsChildRunning())
    spawner.Wait(devtools_goma::Spawner::WAIT_INFINITE);

  char* next_token;
  char* token = strtok_s(&output[0], "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ(".\\dump_env.exe", token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("arg1", token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("arg2", token);

  // These envs are come from "cmd /c" prepending
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_TRUE(absl::StartsWith(token, "COMSPEC="));
  EXPECT_EQ("comspec=c:\\windows\\system32\\cmd.exe",
            absl::AsciiStrToLower(token));

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("PROMPT=$P$G", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("TEST_STRING1=goma", token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("TEST_STRING2=win", token);
}

TEST(SpawnerWin, SpawnerAbspath) {
  char buffer[PATH_MAX] = {0};
  GetModuleFileNameA(nullptr, buffer, PATH_MAX);
  *strrchr(buffer, '\\') = 0;

  std::string cwd(buffer);
  std::string prog(file::JoinPathRespectAbsolute(cwd, "dump_env.exe"));
  std::vector<std::string> argv{prog};
  std::vector<std::string> env{"PATH=" + devtools_goma::GetEnv("PATH"),
                               "PATHEXT=" + devtools_goma::GetEnv("PATHEXT")};

  devtools_goma::SpawnerWin spawner;
  std::string output;
  spawner.SetConsoleOutputBuffer(&output,
                                 devtools_goma::Spawner::MERGE_STDOUT_STDERR);
  int pid = spawner.Run(prog, argv, env, cwd);
  EXPECT_NE(0, pid);
  while (spawner.IsChildRunning())
    spawner.Wait(devtools_goma::Spawner::WAIT_INFINITE);

  char* next_token;
  char* token = strtok_s(&output[0], "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_EQ(prog, token);
}

TEST(SpawnerWin, SpawnerEscapeArgs) {
  char buffer[PATH_MAX] = {0};
  GetModuleFileNameA(nullptr, buffer, PATH_MAX);
  *strrchr(buffer, '\\') = 0;

  std::string cwd(buffer);
  std::string prog(".\\dump_env.exe");
  std::vector<std::string> argv, env;
  argv.push_back("dump_env.exe");
  argv.push_back(
      "-imsvcC:\\Program Files (x86)\\Microsoft Visual Studio 14.0"
      "\\VC\\INCLUDE");
  argv.push_back(
      "-imsvcC:\\Program Files (x86)\\Windows Kits"
      "\\10\\include\\10.0.14393.0\\um");
  argv.push_back(
      "-DSTR=\"str\"");
  argv.push_back("-D\"STR2=\\\"str2\\\"\"");

  argv.push_back("-D\"SPACE=\\\"space \\\"\"");

  argv.push_back("-DFOO=\\\"BAR\\\"");

  // empty string
  argv.push_back("");

  // unbalanced
  argv.push_back("\"");

  // unbalanced with space
  argv.push_back("\" ");

  // backslash in unbalanced
  argv.push_back("\" \\");

  argv.push_back("\" \\\\\\\\\\\\");

  // even number backslash
  argv.push_back("a\\\\\"b");

  // test cases from https://msdn.microsoft.com/en-us/library/17w5ykft(v=vs.85).aspx
  argv.push_back("a\\\\\\b");
  argv.push_back("de fg");
  argv.push_back("a\\\"b");
  argv.push_back("a\\\\b c");

  // TODO: remove these when spawn_win do not find command.
  env.push_back("PATH=" + devtools_goma::GetEnv("PATH"));
  env.push_back("PATHEXT=" + devtools_goma::GetEnv("PATHEXT"));

  // priority not supported yet
  // req.set_priority(devtools_goma::SubProcessReq_Priority_HIGH_PRIORITY);

  devtools_goma::SpawnerWin spawner;
  std::string output;
  spawner.SetConsoleOutputBuffer(&output,
                                 devtools_goma::Spawner::MERGE_STDOUT_STDERR);
  int pid = spawner.Run(prog, argv, env, cwd);
  EXPECT_NE(0, pid);

  Spawner::ProcessStatus process_status = Spawner::ProcessStatus::RUNNING;
  while (spawner.IsChildRunning()) {
    process_status = spawner.Wait(devtools_goma::Spawner::WAIT_INFINITE);
  }

  EXPECT_EQ(Spawner::ProcessStatus::EXITED, process_status);

  char* next_token;
  char* token = strtok_s(&output[0], "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ(
      "-imsvcC:\\Program Files (x86)\\Microsoft Visual Studio 14.0"
      "\\VC\\INCLUDE",
      token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ(
      "-imsvcC:\\Program Files (x86)\\Windows Kits"
      "\\10\\include\\10.0.14393.0\\um",
      token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("-DSTR=\"str\"", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("-D\"STR2=\\\"str2\\\"\"", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("-D\"SPACE=\\\"space \\\"\"", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("-DFOO=\\\"BAR\\\"", token);


  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);

  // empty string check first
  size_t tok_idx = token - &output[0];
  // strtok_s skip \r\n
  ASSERT_GE(tok_idx - 4, 0);
  EXPECT_EQ(output[tok_idx - 4], '\0');
  EXPECT_EQ(output[tok_idx - 3], '\n');
  EXPECT_EQ(output[tok_idx - 2], '\r');
  EXPECT_EQ(output[tok_idx - 1], '\n');

  EXPECT_STREQ("\"", token);


  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("\" ", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("\" \\", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("\" \\\\\\\\\\\\", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  // `a\\"b`
  EXPECT_STREQ("a\\\\\"b", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  // `a\\\b`
  EXPECT_STREQ("a\\\\\\b", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("de fg", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  // `a\"b`
  EXPECT_STREQ("a\\\"b", token);

  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  // `a\\b c`
  EXPECT_STREQ("a\\\\b c", token);
}

// This is test for regression happens on passing path longer than
// MAX_PATH to _splitpath_s.
TEST(SpawnerWin, SpawnerLongArgs) {
  char buffer[PATH_MAX] = {0};
  GetModuleFileNameA(nullptr, buffer, PATH_MAX);
  *strrchr(buffer, '\\') = 0;

  std::string cwd(buffer);
  std::string prog(".\\dump_env.exe");
  std::vector<std::string> argv, env;
  argv.push_back(prog);
  argv.push_back(std::string(MAX_PATH + 10, 'a'));

  // TODO: remove these when spawn_win do not find command.
  env.push_back("PATH=" + devtools_goma::GetEnv("PATH"));
  env.push_back("PATHEXT=" + devtools_goma::GetEnv("PATHEXT"));

  // priority not supported yet
  // req.set_priority(devtools_goma::SubProcessReq_Priority_HIGH_PRIORITY);

  devtools_goma::SpawnerWin spawner;
  std::string output;
  spawner.SetConsoleOutputBuffer(&output,
                                 devtools_goma::Spawner::MERGE_STDOUT_STDERR);
  int pid = spawner.Run(prog, argv, env, cwd);
  EXPECT_NE(0, pid);
  Spawner::ProcessStatus process_status = Spawner::ProcessStatus::RUNNING;
  while (spawner.IsChildRunning()) {
    process_status = spawner.Wait(devtools_goma::Spawner::WAIT_INFINITE);
  }

  EXPECT_EQ(Spawner::ProcessStatus::EXITED, process_status);

  char* next_token;
  char* token = strtok_s(&output[0], "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_EQ(std::string(MAX_PATH + 10, 'a'), token);
}

TEST(SpawnerWin, SpawnerFailed) {
  std::string cwd = "c:\\";
  std::string prog("dump_env.exe");
  std::vector<std::string> argv, env;
  argv.push_back("dump_env.exe");
  argv.push_back("arg1");
  argv.push_back("arg2");
  env.push_back("TEST_STRING1=goma");
  env.push_back("TEST_STRING2=win");
  // TODO: remove these when spawn_win do not find command.
  env.push_back("PATH=C:\\non_exist_folder;C:\\non_exist_folder2");
  env.push_back("PATHEXT=" + devtools_goma::GetEnv("PATHEXT"));

  devtools_goma::SpawnerWin spawner;
  std::string output;
  spawner.SetConsoleOutputBuffer(&output,
                                 devtools_goma::Spawner::MERGE_STDOUT_STDERR);
  int pid = spawner.Run(prog, argv, env, cwd);
  EXPECT_EQ(0, pid);
}

}  // namespace devtools_goma
