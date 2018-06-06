// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "subprocess_task.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/synchronization/notification.h"
#include "basictypes.h"
#include "callback.h"
#include "lockhelper.h"
#include "mypath.h"
#include "platform_thread.h"
#include "subprocess_controller.h"
#include "subprocess_controller_client.h"
#include "util.h"
#include "worker_thread_manager.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

namespace devtools_goma {

class SubProcessTaskTest : public ::testing::Test {
 public:
  SubProcessTaskTest() {}

 protected:
  class SubProcessContext {
   public:
    SubProcessContext(string trace_id,
                      const char* prog,
                      const char* const* argv)
        : trace_id_(std::move(trace_id)),
          prog_(prog),
          argv_(argv),
          s_(nullptr),
          status_(-256),
          done_(false) {}
    ~SubProcessContext() {
    }

    const string trace_id_;
    const char* prog_;
    const char* const * argv_;
    SubProcessTask* s_;
    absl::Notification started_;
    int status_;
    bool done_;

   private:
    DISALLOW_COPY_AND_ASSIGN(SubProcessContext);
  };

  void SetUp() override {
    CheckTempDirectory(GetGomaTmpDir());
    SubProcessController::Options options;
    SubProcessController::Initialize(
        "subprocess_task_unittest", options);
    wm_ = absl::make_unique<WorkerThreadManager>();
    wm_->Start(1);
    SubProcessControllerClient::Initialize(wm_.get(), GetGomaTmpDir());
    int max_wait = 100;
    while (!SubProcessControllerClient::IsRunning() ||
           !SubProcessControllerClient::Get()->Initialized()) {
      PlatformThread::Sleep(1000);
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
        NewCallback(
            this, &SubProcessTaskTest::TestReadCommandOutput, &done),
        WorkerThreadManager::PRIORITY_LOW);
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
#ifndef _WIN32
    EXPECT_EQ("hello\n",
              SubProcessTask::ReadCommandOutput("/bin/echo", argv, env, "",
                                                MERGE_STDOUT_STDERR, nullptr));
#else
    // TODO: remove env after I revise redirector_win.cc.
    env.push_back("PATHEXT=" + GetEnv("PATHEXT"));
    env.push_back("PATH=" + GetEnv("PATH"));
    EXPECT_EQ("hello\r\n",
              SubProcessTask::ReadCommandOutput("cmd", argv, env, "",
                                                MERGE_STDOUT_STDERR, nullptr));
#endif
    SignalDone(done);
  }

  void RunTestSubProcessTrue() {
#ifndef _WIN32
    const char* const argv[] = {"true", nullptr};
#else
    const char* const argv[] = {"cmd", "/c", "exit", "0", nullptr};
#endif
    static const char* kTruePath =
#ifdef __MACH__
      "/usr/bin/true";
#elif !_WIN32
      "/bin/true";
#else
      "cmd";
#endif
    SubProcessContext c("true", kTruePath, argv);
    EXPECT_NE(0, c.status_);
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(
            this, &SubProcessTaskTest::TestSubProcess, &c),
        WorkerThreadManager::PRIORITY_LOW);
    WaitDone(&c.done_);
    EXPECT_EQ(0, c.status_);
  }

  void RunTestSubProcessFalse() {
#ifndef _WIN32
    const char* const argv[] = {"false", nullptr};
#else
    const char* const argv[] = {"cmd", "/c", "exit", "1", nullptr};
#endif
    static const char* kFalsePath =
#ifdef __MACH__
      "/usr/bin/false";
#elif !_WIN32
      "/bin/false";
#else
      "cmd";
#endif
    SubProcessContext c("false", kFalsePath, argv);
    EXPECT_NE(0, c.status_);
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(
            this, &SubProcessTaskTest::TestSubProcess, &c),
        WorkerThreadManager::PRIORITY_LOW);
    WaitDone(&c.done_);
    EXPECT_EQ(1, c.status_);
  }

  void RunTestSubProcessKill() {
#ifndef _WIN32
    const char* const argv[] = {"sleep", "100", nullptr};
    SubProcessContext c("sleep", "/bin/sleep", argv);
#else
    const char* const argv[] = {"cmd", "/c", "timeout", "/t", "1", "/nobreak",
                                ">NUL", nullptr};
    SubProcessContext c("sleep", "cmd", argv);
#endif
    EXPECT_NE(0, c.status_);
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(
            this, &SubProcessTaskTest::TestSubProcess, &c),
        WorkerThreadManager::PRIORITY_LOW);

    ASSERT_TRUE(c.started_.WaitForNotificationWithTimeout(absl::Seconds(10)));
    while (SubProcessState::PENDING == c.s_->state()) {
      PlatformThread::Sleep(100);
    }

    EXPECT_EQ(SubProcessState::RUN, c.s_->state());
    wm_->RunClosure(
        FROM_HERE,
        NewCallback(
            this, &SubProcessTaskTest::TestSubProcessKill, &c),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
    WaitDone(&c.done_);
    EXPECT_EQ(1, c.status_);
  }

  void TestSubProcess(SubProcessContext* c) {
    EXPECT_TRUE(c->s_ == nullptr);
    EXPECT_FALSE(c->done_);
    c->s_ = new SubProcessTask(c->trace_id_, c->prog_,
                               const_cast<char* const *>(c->argv_));
    c->s_->mutable_req()->set_cwd(
        SubProcessControllerClient::Get()->TmpDir());
    EXPECT_EQ(SubProcessState::SETUP, c->s_->state());
#ifdef _WIN32
    // TODO: remove env after I revise redirector_win.cc.
    c->s_->mutable_req()->add_env("PATH=" + GetEnv("PATH"));
    c->s_->mutable_req()->add_env("PATHEXT=" + GetEnv("PATHEXT"));
#endif
    c->s_->Start(
        NewCallback(
            this, &SubProcessTaskTest::TestSubProcessDone, c));
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

TEST_F(SubProcessTaskTest, SubProcessKill) {
  RunTestSubProcessKill();
}

}  // namespace devtools_goma
