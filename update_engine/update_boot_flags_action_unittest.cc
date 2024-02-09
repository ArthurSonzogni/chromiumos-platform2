// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_boot_flags_action.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <gtest/gtest.h>

#include "update_engine/common/fake_boot_control.h"
#include "update_engine/common/fake_hardware.h"

namespace chromeos_update_engine {

class UpdateBootFlagsActionTest : public ::testing::Test {
 protected:
  FakeBootControl boot_control_;
  FakeHardware hardware_;
};

TEST_F(UpdateBootFlagsActionTest, SimpleTest) {
  auto action =
      std::make_unique<UpdateBootFlagsAction>(&boot_control_, &hardware_);
  ActionProcessor processor;
  processor.EnqueueAction(std::move(action));

  EXPECT_FALSE(UpdateBootFlagsAction::updated_boot_flags_);
  EXPECT_FALSE(UpdateBootFlagsAction::is_running_);
  processor.StartProcessing();
  EXPECT_TRUE(UpdateBootFlagsAction::updated_boot_flags_);
  EXPECT_FALSE(UpdateBootFlagsAction::is_running_);
}

TEST_F(UpdateBootFlagsActionTest, RunningMiniOSTest) {
  // Reset the static flags.
  UpdateBootFlagsAction::updated_boot_flags_ = false;
  UpdateBootFlagsAction::is_running_ = false;

  auto action =
      std::make_unique<UpdateBootFlagsAction>(&boot_control_, &hardware_);
  ActionProcessor processor;
  processor.EnqueueAction(std::move(action));
  hardware_.SetIsRunningFromMiniOs(true);

  // Skip updating flags when running with MiniOS.
  processor.StartProcessing();
  EXPECT_FALSE(UpdateBootFlagsAction::updated_boot_flags_);
  EXPECT_FALSE(UpdateBootFlagsAction::is_running_);
}

TEST_F(UpdateBootFlagsActionTest, DoubleActionTest) {
  // Reset the static flags.
  UpdateBootFlagsAction::updated_boot_flags_ = false;
  UpdateBootFlagsAction::is_running_ = false;

  auto action1 =
      std::make_unique<UpdateBootFlagsAction>(&boot_control_, &hardware_);
  auto action2 =
      std::make_unique<UpdateBootFlagsAction>(&boot_control_, &hardware_);
  ActionProcessor processor1, processor2;
  processor1.EnqueueAction(std::move(action1));
  processor2.EnqueueAction(std::move(action2));

  EXPECT_FALSE(UpdateBootFlagsAction::updated_boot_flags_);
  EXPECT_FALSE(UpdateBootFlagsAction::is_running_);
  processor1.StartProcessing();
  EXPECT_TRUE(UpdateBootFlagsAction::updated_boot_flags_);
  EXPECT_FALSE(UpdateBootFlagsAction::is_running_);
  processor2.StartProcessing();
  EXPECT_TRUE(UpdateBootFlagsAction::updated_boot_flags_);
  EXPECT_FALSE(UpdateBootFlagsAction::is_running_);
}

}  // namespace chromeos_update_engine
