// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "minios/mock_process_manager.h"
#include "minios/recovery_installer.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace minios {

class RecoveryInstallerTest : public ::testing::Test {
 protected:
  std::shared_ptr<MockProcessManager> mock_process_manager_ =
      std::make_shared<MockProcessManager>();
  RecoveryInstaller recovery_installer_{mock_process_manager_};
};

TEST_F(RecoveryInstallerTest, RepartitionDiskProcessFailure) {
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), Return(false)));
  EXPECT_FALSE(recovery_installer_.RepartitionDisk());
}

TEST_F(RecoveryInstallerTest, RepartitionDiskExitFailure) {
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(true)));
  EXPECT_FALSE(recovery_installer_.RepartitionDisk());
}

TEST_F(RecoveryInstallerTest, RepeatedRepartitionDisk) {
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), Return(true)));
  EXPECT_TRUE(recovery_installer_.RepartitionDisk());

  // Does not call to repartition the disk again since it completed successfully
  // last time. Still returns true.
  EXPECT_TRUE(recovery_installer_.RepartitionDisk());
}

}  // namespace minios
