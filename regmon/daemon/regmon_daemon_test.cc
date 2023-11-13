// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "regmon/daemon/regmon_daemon.h"

#include <gtest/gtest.h>

namespace regmon {

class RegmonDaemonTest : public ::testing::Test {
 public:
  RegmonDaemonTest() = default;

 protected:
  std::unique_ptr<RegmonDaemon> regmond_;
};

TEST_F(RegmonDaemonTest, InitService) {
  ASSERT_TRUE(true);
}

}  // namespace regmon
