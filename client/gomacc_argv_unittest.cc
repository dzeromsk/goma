// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "gomacc_argv.h"

#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <string>
#include <vector>

#include "file.h"
#include "ioutil.h"
#include "mypath.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

namespace devtools_goma {

#ifndef _WIN32
TEST(GomaccArgvTest, BuildGomaccArgvMasqueradeGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(3, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_TRUE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvMasqueradeClang) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"/gomadir/clang", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(3, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("clang", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_TRUE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependBaseGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvFullPathPrependBaseGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"/gomadir/gomacc", "gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependPathGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "path/gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("path/gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("path/gcc", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependFullPathGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "/usr/bin/gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("/usr/bin/gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("/usr/bin/gcc", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvFullPathPrependPathGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"/gomadir/gomacc", "path/gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("path/gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("path/gcc", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvFullPathPrependFullPathGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"/gomadir/gomacc", "/usr/bin/gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("/usr/bin/gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("/usr/bin/gcc", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvMasqueradeVerifyCommandGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gcc", "--goma-verify-command", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(4U, args.size());
  EXPECT_EQ("gcc", args[0]);
  EXPECT_EQ("--goma-verify-command", args[1]);
  EXPECT_EQ("-c", args[2]);
  EXPECT_EQ("hello.c", args[3]);
  EXPECT_TRUE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependVerifyCommandGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "--goma-verify-command",
                        "gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(5, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_EQ("all", verify_command);
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependVerifyCommandVersionGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "--goma-verify-command=version",
                        "gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(5, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_EQ("version", verify_command);
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependVerifyCommandChecksumFullPathGcc) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "--goma-verify-command=checksum",
                        "/usr/bin/gcc", "-c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(5, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("/usr/bin/gcc", args[0]);
  EXPECT_EQ("-c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_EQ("checksum", verify_command);
  EXPECT_EQ("/usr/bin/gcc", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependFlag) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "-c", "hello.c"};

  EXPECT_FALSE(BuildGomaccArgv(3, argv,
                               &args, &masquerade_mode,
                               &verify_command, &local_command_path));
}

TEST(GomaccArgvTest, BuildGomaccArgvMasqueradeNoCompiler) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"echo", "test"};

  EXPECT_TRUE(BuildGomaccArgv(2, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(2U, args.size());
  EXPECT_EQ("echo", args[0]);
  EXPECT_EQ("test", args[1]);
  EXPECT_TRUE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvMasqueradeFullPathNoCompiler) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"/gomadir/echo", "test"};

  EXPECT_TRUE(BuildGomaccArgv(2, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(2U, args.size());
  EXPECT_EQ("echo", args[0]);
  EXPECT_EQ("test", args[1]);
  EXPECT_TRUE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependBaseNoCompiler) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "echo", "test"};

  EXPECT_TRUE(BuildGomaccArgv(3, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(2U, args.size());
  EXPECT_EQ("echo", args[0]);
  EXPECT_EQ("test", args[1]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependoCompiler) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "/bin/echo", "test"};

  EXPECT_TRUE(BuildGomaccArgv(3, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(2U, args.size());
  EXPECT_EQ("/bin/echo", args[0]);
  EXPECT_EQ("test", args[1]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("/bin/echo", local_command_path);
}

#else  // _WIN32
TEST(GomaccArgvTest, BuildGomaccArgvMasqueradeCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"c:\\gomadir\\cl.exe", "/c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(3, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("cl.exe", args[0]);
  EXPECT_EQ("/c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_TRUE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependBaseCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc.exe", "cl", "/c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("cl", args[0]);
  EXPECT_EQ("/c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvFullPathPrependBaseCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"c:\\gomadir\\gomacc.exe", "cl", "/c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("cl", args[0]);
  EXPECT_EQ("/c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_TRUE(local_command_path.empty());
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependPathCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "path\\cl", "/c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("path\\cl", args[0]);
  EXPECT_EQ("/c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("path\\cl", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependFullPathCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "c:\\vc\\bin\\cl", "/c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("c:\\vc\\bin\\cl", args[0]);
  EXPECT_EQ("/c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("c:\\vc\\bin\\cl", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvFullPathPrependPathCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"c:\\gomadir\\gomacc", "path\\cl", "/c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("path\\cl", args[0]);
  EXPECT_EQ("/c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("path\\cl", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvFullPathPrependFullPathCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"c:\\gomadir\\gomacc",
                        "c:\\vc\\bin\\cl", "/c", "hello.c"};

  EXPECT_TRUE(BuildGomaccArgv(4, argv,
                              &args, &masquerade_mode,
                              &verify_command, &local_command_path));
  EXPECT_EQ(3U, args.size());
  EXPECT_EQ("c:\\vc\\bin\\cl", args[0]);
  EXPECT_EQ("/c", args[1]);
  EXPECT_EQ("hello.c", args[2]);
  EXPECT_FALSE(masquerade_mode);
  EXPECT_TRUE(verify_command.empty());
  EXPECT_EQ("c:\\vc\\bin\\cl", local_command_path);
}

TEST(GomaccArgvTest, BuildGomaccArgvPrependNoCl) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc", "/c", "hello.c"};

  EXPECT_FALSE(BuildGomaccArgv(3, argv,
                               &args, &masquerade_mode,
                               &verify_command, &local_command_path));
}
#endif  // _WIN32

TEST(GomaccArgvTest, BuildGomaccArgvNoCompiler) {
  std::vector<string> args;
  bool masquerade_mode;
  string verify_command;
  string local_command_path;
  const char* argv[] = {"gomacc"};

  EXPECT_FALSE(BuildGomaccArgv(1, argv,
                               &args, &masquerade_mode,
                               &verify_command, &local_command_path));
}

#ifdef _WIN32

TEST(GomaccArgvTest, FanOutArgsByInput) {
  std::vector<string> args;
  args.push_back("cl");
  args.push_back("/c");
  args.push_back("/DFOO");
  args.push_back("/Ic:\\vc\\include");
  args.push_back("/Fo..\\obj\\");
  args.push_back("/Fdfoo.pdb");
  args.push_back("foo.cpp");
  args.push_back("bar.cpp");
  args.push_back("baz.cpp");
  args.push_back("/MP");

  std::set<string> input_filenames;
  input_filenames.insert("foo.cpp");
  input_filenames.insert("bar.cpp");
  input_filenames.insert("baz.cpp");

  std::vector<string> args_no_input;
  FanOutArgsByInput(args, input_filenames, &args_no_input);
  EXPECT_EQ(6U, args_no_input.size());
  EXPECT_EQ("/c", args_no_input[0]);
  EXPECT_EQ("/DFOO", args_no_input[1]);
  EXPECT_EQ("/Ic:\\vc\\include", args_no_input[2]);
  EXPECT_EQ("/Fo..\\obj\\", args_no_input[3]);
  EXPECT_EQ("/Fdfoo.pdb", args_no_input[4]);
  EXPECT_EQ("/MP", args_no_input[5]);
}

TEST(GomaccArgvTest, BuildArgsForInput) {
  std::vector<string> args_no_input;
  args_no_input.push_back("/c");
  args_no_input.push_back("/DFOO=\"foo.h\"");
  args_no_input.push_back("/Ic:\\vc\\include");
  args_no_input.push_back("/Fo..\\obj\\");
  args_no_input.push_back("/Fdfoo.pdb");
  args_no_input.push_back("/MP");

  string cmdline = BuildArgsForInput(args_no_input, "foo.cpp");
  EXPECT_EQ("\"/c\" \"/DFOO=\\\"foo.h\\\"\" \"/Ic:\\vc\\include\" "
            "\"/Fo..\\obj\\\\\" \"/Fdfoo.pdb\" \"/MP\" \"foo.cpp\"", cmdline);
}

TEST(GomaccArgvTest, EscapeWinArg) {
  EXPECT_EQ("\"foo\"", EscapeWinArg("foo"));
  EXPECT_EQ("\"foo\\bar\"", EscapeWinArg("foo\\bar"));
  EXPECT_EQ("\"foo bar\"", EscapeWinArg("foo bar"));
  EXPECT_EQ("\"foo=\\\"bar\\\"\"", EscapeWinArg("foo=\"bar\""));
  EXPECT_EQ("\"foo\\\\\"", EscapeWinArg("foo\\"));
  EXPECT_EQ("\"foo\\\\\\\"", EscapeWinArg("foo\\\\"));
}

#endif

}  // namespace devtools_goma
