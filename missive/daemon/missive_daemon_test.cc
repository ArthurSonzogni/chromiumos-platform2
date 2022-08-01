// Copyright 2022 The Chromium OS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/daemon/missive_daemon.h"

#include <memory>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace reporting {

class MissiveDaemonTest : public ::testing::Test {
 public:
  MissiveDaemonTest() = default;

 protected:
  std::unique_ptr<MissiveDaemon> missived_;
  base::test::TaskEnvironment task_environment_;
};

TEST_F(MissiveDaemonTest, DummyTest) {}

}  // namespace reporting
