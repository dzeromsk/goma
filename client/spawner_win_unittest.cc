// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// This is a Windows-only unit test
#ifdef _WIN32
#include "spawner_win.h"

#include "compiler_specific.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()
#include "util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

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
  fgets(temp, PATH_MAX, fp);
  EXPECT_STREQ("arg1\n", temp);
  fgets(temp, PATH_MAX, fp);
  EXPECT_STREQ("arg2\n", temp);
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
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("arg1", token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("arg2", token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("TEST_STRING1=goma", token);
  token = strtok_s(nullptr, "\r\n", &next_token);
  EXPECT_TRUE(token != nullptr);
  EXPECT_STREQ("TEST_STRING2=win", token);
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

#endif  // _WIN32
