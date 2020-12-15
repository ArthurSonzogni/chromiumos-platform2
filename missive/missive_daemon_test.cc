// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive_daemon.h"

#include <memory>

#include <brillo/brillo_export.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace reporting {

class MissiveDaemonTest : public ::testing::Test {
 public:
  MissiveDaemonTest() = default;

 protected:
  std::unique_ptr<MissiveDaemon> missived_;
};

TEST_F(MissiveDaemonTest, InitServiceContext) {
  // TODO(zatrudo): Create tests similar to chromium
  ASSERT_TRUE(true);
}

}  // namespace reporting
