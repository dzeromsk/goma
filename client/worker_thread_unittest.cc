// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "worker_thread.h"

#include "absl/time/time.h"
#include "compiler_specific.h"
#include "socket_descriptor.h"
#include "glog/logging.h"

#include <gtest/gtest.h>

namespace devtools_goma {

class WorkerThreadTest : public ::testing::Test {
 protected:
  void TestDelayedClosureQueue() {
    WorkerThread::DelayedClosureQueue delayed_pendings;
    WorkerThread::DelayedClosureImpl *first
        = new WorkerThread::DelayedClosureImpl("first", absl::Seconds(1),
                                               nullptr);
    WorkerThread::DelayedClosureImpl *second
        = new WorkerThread::DelayedClosureImpl("second", absl::Seconds(2),
                                               nullptr);
    WorkerThread::DelayedClosureImpl *third
        = new WorkerThread::DelayedClosureImpl("third", absl::Seconds(3),
                                               nullptr);
    ASSERT_TRUE(delayed_pendings.empty());
    delayed_pendings.push(first);
    delayed_pendings.push(third);
    delayed_pendings.push(second);

    EXPECT_EQ(3U, delayed_pendings.size());
    WorkerThread::DelayedClosureImpl* dci = nullptr;
    ASSERT_TRUE(!delayed_pendings.empty());
    dci = delayed_pendings.top();
    EXPECT_EQ(first, dci);
    delayed_pendings.pop();
    dci->Run();

    ASSERT_TRUE(!delayed_pendings.empty());
    dci = delayed_pendings.top();
    EXPECT_EQ(second, dci);
    delayed_pendings.pop();
    dci->Run();

    ASSERT_TRUE(!delayed_pendings.empty());
    dci = delayed_pendings.top();
    EXPECT_EQ(third, dci);
    delayed_pendings.pop();
    dci->Run();

    ASSERT_TRUE(delayed_pendings.empty());
  }
};

TEST_F(WorkerThreadTest, DelayedClosureQueue) {
  TestDelayedClosureQueue();
}

}  // namespace devtools_goma
