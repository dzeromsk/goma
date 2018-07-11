// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spawner_posix.h"

#include <unistd.h>

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
  const int monitor_pid = spawner.Run(args[0], args, envs, ".");
  EXPECT_NE(Spawner::kInvalidPid, spawner.monitor_pid());
  EXPECT_NE(Spawner::kInvalidPid, spawner.prog_pid());
  EXPECT_NE(spawner.monitor_pid(), spawner.prog_pid());

  EXPECT_EQ(spawner.monitor_pid(), monitor_pid);

  EXPECT_EQ(Spawner::ProcessStatus::EXITED,
            spawner.Wait(Spawner::WAIT_INFINITE));

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

  EXPECT_EQ(Spawner::ProcessStatus::EXITED,
            spawner.Wait(Spawner::WAIT_INFINITE));

  EXPECT_FALSE(spawner.IsChildRunning());
  EXPECT_EQ(1, spawner.ChildStatus());

  EXPECT_FALSE(spawner.IsSignaled());
}

TEST(SpawnerPosix, RunTestMissing) {
  SpawnerPosix spawner;
  const std::string non_existent_path = "/path/should/not/exist";
  ASSERT_NE(0, access(non_existent_path.c_str(), F_OK));
  const std::vector<std::string> args { non_existent_path };
  const std::vector<std::string> envs;
  EXPECT_NE(Spawner::kInvalidPid, spawner.Run(args[0], args, envs, "."));

  // When a non-existing program is invoked, it's immediately exited.
  EXPECT_EQ(Spawner::ProcessStatus::EXITED,
            spawner.Wait(Spawner::WAIT_INFINITE));

  EXPECT_FALSE(spawner.IsChildRunning());

  // In the case a non-existent program is passed to posix_spawn,
  // its behavior can be different by os.
  //
  // If posix_spawn failed, status is -256.
  // Even if posix_spawn succeeds, a program fails start.
  // In that case, exit status is 127 (according to man).
  // Either case can happen.
  EXPECT_TRUE(spawner.ChildStatus() == -256 || spawner.ChildStatus() == 127);

  EXPECT_FALSE(spawner.IsSignaled());
}

TEST(SpawnerPosix, RunKillTest) {
  SpawnerPosix spawner;
  const std::vector<std::string> args{"/bin/sleep", "10"};
  const std::vector<std::string> envs;
  EXPECT_NE(Spawner::kInvalidPid, spawner.Run(args[0], args, envs, "."));

  EXPECT_EQ(Spawner::ProcessStatus::RUNNING, spawner.Wait(Spawner::NO_HANG));

  EXPECT_EQ(Spawner::ProcessStatus::RUNNING, spawner.Kill());
  EXPECT_EQ(Spawner::ProcessStatus::EXITED,
            spawner.Wait(Spawner::WAIT_INFINITE));

  EXPECT_FALSE(spawner.IsChildRunning());
  EXPECT_EQ(1, spawner.ChildStatus());

  EXPECT_TRUE(spawner.IsSignaled());
  EXPECT_EQ(SIGINT, spawner.ChildTermSignal());
}

TEST(SpawnerPosix, RunKillWaitTest) {
  SpawnerPosix spawner;
  const std::vector<std::string> args{"/bin/sleep", "10"};
  const std::vector<std::string> envs;
  EXPECT_NE(Spawner::kInvalidPid, spawner.Run(args[0], args, envs, "."));

  EXPECT_EQ(Spawner::ProcessStatus::EXITED, spawner.Wait(Spawner::NEED_KILL));

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

  int monitor_process_id = spawner.Run(args[0], args, envs, ".");
  EXPECT_NE(Spawner::kInvalidPid, monitor_process_id);
  EXPECT_NE(Spawner::kInvalidPid, spawner.prog_pid());

  // check session id
  pid_t mysid = getsid(0);
  ASSERT_NE(mysid, -1);

  pid_t detached_sid = getsid(spawner.prog_pid());

  // Should not have same session with detached process.
  EXPECT_NE(detached_sid, -1);
  EXPECT_NE(detached_sid, mysid);
}

}  // namespace devtools_goma
