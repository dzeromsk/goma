// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spawner_posix.h"

#include "gtest/gtest.h"

namespace devtools_goma {

TEST(SpawnerPosix, RunTrueTest) {
  SpawnerPosix spawner;
#ifdef __MACH__
  const std::vector<std::string> args{"/usr/bin/true"};
#else
  const std::vector<std::string> args{"/bin/true"};
#endif
  const std::vector<std::string> envs;
  EXPECT_NE(Spawner::kInvalidPid, spawner.Run(args[0], args, envs, "."));

  EXPECT_TRUE(spawner.Wait(Spawner::WAIT_INFINITE));

  EXPECT_FALSE(spawner.IsChildRunning());
  EXPECT_FALSE(spawner.IsSignaled());
  EXPECT_EQ(0, spawner.ChildStatus());
}

TEST(SpawnerPosix, RunFalseTest) {
  SpawnerPosix spawner;
#ifdef __MACH__
  const std::vector<std::string> args{"/usr/bin/false"};
#else
  const std::vector<std::string> args{"/bin/false"};
#endif
  const std::vector<std::string> envs;
  EXPECT_NE(Spawner::kInvalidPid, spawner.Run(args[0], args, envs, "."));

  EXPECT_TRUE(spawner.Wait(Spawner::WAIT_INFINITE));

  EXPECT_FALSE(spawner.IsChildRunning());
  EXPECT_EQ(1, spawner.ChildStatus());

  EXPECT_FALSE(spawner.IsSignaled());
}

TEST(SpawnerPosix, RunKillTest) {
  SpawnerPosix spawner;
  const std::vector<std::string> args{"/bin/sleep", "10"};
  const std::vector<std::string> envs;
  EXPECT_NE(Spawner::kInvalidPid, spawner.Run(args[0], args, envs, "."));

  EXPECT_TRUE(spawner.Kill());
  EXPECT_TRUE(spawner.Wait(Spawner::WAIT_INFINITE));

  EXPECT_FALSE(spawner.IsChildRunning());
  EXPECT_EQ(1, spawner.ChildStatus());

  EXPECT_TRUE(spawner.IsSignaled());
  EXPECT_EQ(SIGINT, spawner.ChildTermSignal());
}

// TODO: Enable this test.
TEST(SpawnerPosix, DISABLED_RunKillWaitTest) {
  SpawnerPosix spawner;
  const std::vector<std::string> args{"/bin/sleep", "10"};
  const std::vector<std::string> envs;
  EXPECT_NE(Spawner::kInvalidPid, spawner.Run(args[0], args, envs, "."));

  EXPECT_TRUE(spawner.Wait(Spawner::NEED_KILL));

  EXPECT_FALSE(spawner.IsChildRunning());
  EXPECT_EQ(1, spawner.ChildStatus());

  EXPECT_TRUE(spawner.IsSignaled());
  EXPECT_EQ(SIGINT, spawner.ChildTermSignal());
}

TEST(SpawnerPosix, RunDetachTest) {
  SpawnerPosix spawner;
  const std::vector<std::string> args{"/bin/sleep", "10"};
  const std::vector<std::string> envs;
  spawner.SetDetach(true);

  int spawned_process_id = spawner.Run(args[0], args, envs, ".");
  EXPECT_NE(Spawner::kInvalidPid, spawned_process_id);

  // check session id
  pid_t mysid = getsid(0);
  ASSERT_NE(mysid, -1);

  pid_t detached_sid = getsid(spawned_process_id);

  // Should not have same session with detached process.
  EXPECT_NE(detached_sid, -1);
  EXPECT_NE(detached_sid, mysid);
}

}  // namespace devtools_goma
