// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/heartd.h"

#include <sysexits.h>

#include <gtest/gtest.h>

namespace heartd {

// This unittests only test the scenario when heartd fails to open the
// /proc/sysrq-trigger file.
//
// For the left part, we can test them in each class except for the mojo service
// manager registration. But that is covered by Tast test: heartd.Registration.
class HeartdDaemonTest : public testing::Test {
 public:
  HeartdDaemonTest() {}
  ~HeartdDaemonTest() override = default;

  int StartService(int sysrq_fd) {
    heartd_ = std::make_unique<HeartdDaemon>(-1);
    return heartd_->OnEventLoopStarted();
  }

 protected:
  std::unique_ptr<HeartdDaemon> heartd_;
};

TEST_F(HeartdDaemonTest, InvalidFd) {
  EXPECT_EQ(StartService(-1), EX_UNAVAILABLE);
}

}  // namespace heartd
