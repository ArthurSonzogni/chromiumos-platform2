// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <memory>

#include "minios/mock_process_manager.h"
#include "minios/recovery_installer.h"

using testing::_;

namespace minios {

class RecoveryInstallerTest : public ::testing::Test {
 protected:
  std::shared_ptr<MockProcessManager> mock_process_manager_ =
      std::make_shared<MockProcessManager>();
  RecoveryInstaller recovery_installer_{mock_process_manager_};
};

TEST_F(RecoveryInstallerTest, RepartitionDiskFailure) {
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::Return(false));
  EXPECT_FALSE(recovery_installer_.RepartitionDisk());
}

TEST_F(RecoveryInstallerTest, RepeatedRepartitionDisk) {
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::Return(true));
  EXPECT_TRUE(recovery_installer_.RepartitionDisk());

  // Does not call to repartition the disk again since it completed successfully
  // last time. Still returns true.
  EXPECT_TRUE(recovery_installer_.RepartitionDisk());
}

}  // namespace minios
