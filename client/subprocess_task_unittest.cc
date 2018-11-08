// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "subprocess_task.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "basictypes.h"
#include "callback.h"
#include "env_flags.h"
#include "file_dir.h"
#include "lockhelper.h"
#include "mypath.h"
#include "path.h"
#include "platform_thread.h"
#include "scoped_tmp_file.h"
#include "subprocess_controller.h"
#include "subprocess_controller_client.h"
#include "unittest_util.h"
#include "util.h"
#include "worker_thread.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

GOMA_DECLARE_bool(COMPILER_PROXY_ENABLE_CRASH_DUMP);

namespace devtools_goma {

class SubProcessTaskTest : public ::testing::Test {
 public:
  SubProcessTaskTest() {}

 protected:
  class SubProcessContext {
   public:
    SubProcessContext(string trace_id,
                      const char* prog,
                      const char* const* argv,
                      const std::vector<string>* env)
        : trace_id_(std::move(trace_id)),
          prog_(prog),
          argv_(argv),
          env_(env),
          s_(nullptr),
          status_(-256),
          done_(false) {}
    ~SubProcessContext() {}

    const string trace_id_;
    const char* prog_;
    const char* const* argv_;
    const std::vector<string>* env_;
    SubProcessTask* s_;
    absl::Notification started_;
    int status_;
    bool done_;

   private:
    DISALLOW_COPY_AND_ASSIGN(SubProcessContext);
  };

  void SetUp() override {
    // crash reporter fails on Mac in test. So, we need to disable it here.
    FLAGS_COMPILER_PROXY_ENABLE_CRASH_DUMP = false;

    CheckTempDirectory(GetGomaTmpDir());

    SubProcessController::Options options;
    SubProcessController::Initialize("subprocess_task_unittest", options);
    wm_ = absl::make_unique<WorkerThreadManager>();
    wm_->Start(1);
    SubProcessControllerClient::Initialize(wm_.get(), GetGomaTmpDir());
    CHECK(SubProcessControllerClient::IsRunning());
    int max_wait = 1000;
    while (!SubProcessControllerClient::Get()->Initialized()) {
      // TODO: use condvar on initialized_ instead of sleep.
      absl::SleepFor(absl::Milliseconds(100));
      if (--max_wait <= 0) {
        LOG(FATAL) << "SubProcessControllerClient not running.";
      }
    }
  }

  void TearDown() override {
    SubProcessControllerClient::Get()->Quit();
    SubProcessControllerClient::Get()->Shutdown();
    wm_->Finish();
    wm_.reset();
  }

  void WaitDone(bool* done) {
    AutoLock lock(&mu_);
    while (!*done) {
      cond_.Wait(&mu_);
    }
  }

  void SignalDone(bool* done) {
    EXPECT_FALSE(*done);
    AutoLock lock(&mu_);
    *done = true;
    cond_.Signal();
  }

  void RunTestReadCommandOutput() {
    bool done = false;
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(this, &SubProcessTaskTest::TestReadCommandOutput, &done),
        WorkerThread::PRIORITY_LOW);
    WaitDone(&done);
  }

  void TestReadCommandOutput(bool* done) {
    EXPECT_FALSE(*done);
    std::vector<string> argv;
#ifdef _WIN32
    argv.push_back("cmd");
    argv.push_back("/c");
#endif
    argv.push_back("echo");
    argv.push_back("hello");
    std::vector<string> env;
#ifdef _WIN32
    // TODO: remove env after I revise redirector_win.cc.
    env.push_back("PATHEXT=" + GetEnv("PATHEXT"));
    env.push_back("PATH=" + GetEnv("PATH"));
    EXPECT_EQ("hello\r\n",
              SubProcessTask::ReadCommandOutput("cmd", argv, env, "",
                                                MERGE_STDOUT_STDERR, nullptr));
#else
    EXPECT_EQ("hello\n",
              SubProcessTask::ReadCommandOutput("/bin/echo", argv, env, "",
                                                MERGE_STDOUT_STDERR, nullptr));
#endif
    SignalDone(done);
  }

  void RunTestSubProcessTrue() {
#ifdef _WIN32
    const char* const argv[] = {"cmd", "/c", "exit", "0", nullptr};
#else
    const char* const argv[] = {"true", nullptr};
#endif
    static const char* kTruePath =
#ifdef __MACH__
        "/usr/bin/true";
#elif !_WIN32
        "/bin/true";
#else
        "cmd";
#endif

    std::vector<string> env;
    SubProcessContext c("true", kTruePath, argv, &env);
    EXPECT_NE(0, c.status_);
    wm_->RunClosure(FROM_HERE,
                    NewCallback(this, &SubProcessTaskTest::TestSubProcess, &c),
                    WorkerThread::PRIORITY_LOW);
    WaitDone(&c.done_);
    EXPECT_EQ(0, c.status_);
  }

  void RunTestSubProcessFalse() {
#ifdef _WIN32
    const char* const argv[] = {"cmd", "/c", "exit", "1", nullptr};
#else
    const char* const argv[] = {"false", nullptr};
#endif
    static const char* kFalsePath =
#ifdef __MACH__
        "/usr/bin/false";
#elif !_WIN32
        "/bin/false";
#else
        "cmd";
#endif

    std::vector<string> env;
    SubProcessContext c("false", kFalsePath, argv, &env);
    EXPECT_NE(0, c.status_);
    wm_->RunClosure(FROM_HERE,
                    NewCallback(this, &SubProcessTaskTest::TestSubProcess, &c),
                    WorkerThread::PRIORITY_LOW);
    WaitDone(&c.done_);
    EXPECT_EQ(1, c.status_);
  }

  void RunTestSubProcessKillSleep() {
    std::vector<string> env;
#ifdef _WIN32
    const char* const argv[] = {"cmd", "/c", "timeout", "/t", "1", "/nobreak",
                                ">NUL", nullptr};
    SubProcessContext c("sleep", "cmd", argv, &env);
#else
    const char* const argv[] = {"sleep", "100", nullptr};
    SubProcessContext c("sleep", "/bin/sleep", argv, &env);
#endif
    EXPECT_NE(0, c.status_);
    wm_->RunClosure(FROM_HERE,
                    NewCallback(this, &SubProcessTaskTest::TestSubProcess, &c),
                    WorkerThread::PRIORITY_LOW);

    ASSERT_TRUE(c.started_.WaitForNotificationWithTimeout(absl::Seconds(10)));
    while (SubProcessState::PENDING == c.s_->state()) {
      absl::SleepFor(absl::Milliseconds(100));
    }

    EXPECT_EQ(SubProcessState::RUN, c.s_->state());
    EXPECT_NE(-1, c.s_->started().pid());

    wm_->RunClosure(
        FROM_HERE,
        NewCallback(this, &SubProcessTaskTest::TestSubProcessKill, &c),
        WorkerThread::PRIORITY_IMMEDIATE);
    WaitDone(&c.done_);
    EXPECT_EQ(1, c.status_);
  }

  // This test tests two behaviors of killing a running Clang subprocess:
  // 1. Clang can be killed correctly: exit with status code 1.
  // 2. Clang doesn't dump any debug files that consume users' disk spaces.
  void RunTestSubProcessKillClang() {
    // In order to kill Clang before it finishes, Clang needs to compile a
    // non-trivial task, and following Tak function turns out to be a function
    // that takes a long time to compile, and compiling it takes more than 6
    // seconds even on a beefy Linux machine as of 2018-7-4, so it is chosen
    // as the target source file for this test.
    //
    // Note that the source file name has to have a valid extension such as
    // ".cc" or otherwise Clang thinks it's an input for linker and exits
    // immediately.
    ScopedTmpFile tmp_source_file("test_kill_clang_subprocess_source_file",
                                  ".cc");
    std::string file_content =
        "template <int x, int y, int z, bool c>"
        "struct TakImpl {"
        "   enum { v = y };"
        "};"
        "template <int x, int y, int z>"
        "struct Tak {"
        "   enum { v = TakImpl<x, y, z, x <= y>::v };"
        "};"
        "template <int x, int y, int z>"
        "struct TakImpl<x, y, z, false> {"
        "   enum { v = Tak<Tak<x-1, y, z>::v, Tak<y-1, z, x>::v, Tak<z-1, x, "
        "y>::v>::v };"
        "};"
        "const int v = Tak<400, 30, 3>::v;";
    ssize_t written =
        tmp_source_file.Write(file_content.data(), file_content.size());
    ASSERT_EQ(file_content.size(), written);
    tmp_source_file.Close();

    string clang_path = GetClangPath();
    ASSERT_TRUE(!clang_path.empty());
    string clang_name = string(file::Basename(clang_path));
    const char* const argv[] = {clang_name.c_str(), "-c",
                                tmp_source_file.filename().c_str(), nullptr};


    // According to
    // https://github.com/llvm-project/llvm-project-20170507/blob/c2d8657324d589f748a075af2f5b8898a6942130/llvm/lib/Support/Unix/Path.inc#L976
    // Clang queries following environmental variables to decide which
    // temporary directory to use for dumping debug files: $TMPDIR, $TMP,
    // $TEMP, $TEMPDIR.
    ScopedTmpDir tmp_clang_dump_dir("clang_dump_dir");
    std::vector<string> env = {"TMPDIR=" + tmp_clang_dump_dir.dirname(),
                               "TMP=" + tmp_clang_dump_dir.dirname(),
                               "TEMP=" + tmp_clang_dump_dir.dirname(),
                               "TEMPDIR=" + tmp_clang_dump_dir.dirname()};

    SubProcessContext c("clang", clang_path.c_str(), argv, &env);
    EXPECT_NE(0, c.status_);
    wm_->RunClosure(FROM_HERE,
                    NewCallback(this, &SubProcessTaskTest::TestSubProcess, &c),
                    WorkerThread::PRIORITY_LOW);

    ASSERT_TRUE(c.started_.WaitForNotificationWithTimeout(absl::Seconds(10)));
    while (SubProcessState::PENDING == c.s_->state()) {
      absl::SleepFor(absl::Milliseconds(10));
    }

    EXPECT_EQ(SubProcessState::RUN, c.s_->state());
    EXPECT_NE(-1, c.s_->started().pid());

    wm_->RunClosure(
        FROM_HERE,
        NewCallback(this, &SubProcessTaskTest::TestSubProcessKill, &c),
        WorkerThread::PRIORITY_IMMEDIATE);
    WaitDone(&c.done_);

    EXPECT_EQ(1, c.status_);
    std::vector<DirEntry> sub_file_or_dirs;
    EXPECT_TRUE(ListDirectory(tmp_clang_dump_dir.dirname(), &sub_file_or_dirs));
    ASSERT_EQ(2, sub_file_or_dirs.size());

    std::sort(
        sub_file_or_dirs.begin(), sub_file_or_dirs.end(),
        [](const DirEntry& l, const DirEntry& r) { return l.name < r.name; });
    EXPECT_EQ(".", sub_file_or_dirs[0].name);
    EXPECT_EQ("..", sub_file_or_dirs[1].name);
  }

  void TestSubProcess(SubProcessContext* c) {
    EXPECT_TRUE(c->s_ == nullptr);
    EXPECT_FALSE(c->done_);
    c->s_ = new SubProcessTask(c->trace_id_, c->prog_,
                               const_cast<char* const*>(c->argv_));
    c->s_->mutable_req()->set_cwd(SubProcessControllerClient::Get()->TmpDir());
    EXPECT_EQ(SubProcessState::SETUP, c->s_->state());

    for (auto env_var : *(c->env_)) {
      c->s_->mutable_req()->add_env(std::move(env_var));
    }

#ifdef _WIN32
    // TODO: remove env after I revise redirector_win.cc.
    c->s_->mutable_req()->add_env("PATH=" + GetEnv("PATH"));
    c->s_->mutable_req()->add_env("PATHEXT=" + GetEnv("PATHEXT"));
#endif
    c->s_->Start(NewCallback(this, &SubProcessTaskTest::TestSubProcessDone, c));
    EXPECT_NE(SubProcessState::SETUP, c->s_->state());

    c->started_.Notify();
  }

  void TestSubProcessDone(SubProcessContext* c) {
    EXPECT_TRUE(c->s_ != nullptr);
    EXPECT_FALSE(c->done_);
    EXPECT_EQ(SubProcessState::FINISHED, c->s_->state());
    EXPECT_EQ(c->s_->req().id(), c->s_->started().id());
    EXPECT_NE(-1, c->s_->started().pid());
    EXPECT_EQ(c->s_->req().id(), c->s_->terminated().id());
    c->status_ = c->s_->terminated().status();
    c->s_ = nullptr;
    SignalDone(&c->done_);
  }

  void TestSubProcessKill(SubProcessContext* c) {
    EXPECT_TRUE(c->s_ != nullptr);
    EXPECT_FALSE(c->done_);
    EXPECT_NE(-1, c->s_->started().pid());
    EXPECT_EQ(SubProcessState::RUN, c->s_->state());
    EXPECT_TRUE(c->s_->Kill());
    EXPECT_EQ(SubProcessState::SIGNALED, c->s_->state());
    EXPECT_FALSE(c->s_->Kill());
  }

  std::unique_ptr<WorkerThreadManager> wm_;
  mutable Lock mu_;
  ConditionVariable cond_;
};

TEST_F(SubProcessTaskTest, ReadCommandOutput) {
  RunTestReadCommandOutput();
}

TEST_F(SubProcessTaskTest, RunTrue) {
  RunTestSubProcessTrue();
}

TEST_F(SubProcessTaskTest, RunFalse) {
  RunTestSubProcessFalse();
}

TEST_F(SubProcessTaskTest, SubProcessKillSleep) {
  RunTestSubProcessKillSleep();
}

TEST_F(SubProcessTaskTest, SubProcessKillClang) {
  RunTestSubProcessKillClang();
}

}  // namespace devtools_goma
