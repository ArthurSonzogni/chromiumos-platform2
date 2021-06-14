// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <gtest/gtest.h>

#include "minios/mock_draw_interface.h"
#include "minios/mock_recovery_installer.h"
#include "minios/mock_screen_controller.h"
#include "minios/mock_update_engine_proxy.h"
#include "minios/screens/screen_download.h"

using ::testing::NiceMock;
using ::testing::StrictMock;

namespace minios {

class ScreenDownloadTest : public ::testing::Test {
 protected:
  std::unique_ptr<MockRecoveryInstaller> mock_recovery_installer_ =
      std::make_unique<MockRecoveryInstaller>();
  MockRecoveryInstaller* mock_recovery_installer_ptr_ =
      mock_recovery_installer_.get();

  std::unique_ptr<MockUpdateEngineProxy> mock_update_engine_proxy_ =
      std::make_unique<NiceMock<MockUpdateEngineProxy>>();
  MockUpdateEngineProxy* mock_update_engine_ptr_ =
      mock_update_engine_proxy_.get();

  std::shared_ptr<MockDrawInterface> mock_draw_interface_ =
      std::make_shared<NiceMock<MockDrawInterface>>();
  MockDrawInterface* mock_draw_interface_ptr_ = mock_draw_interface_.get();

  StrictMock<MockScreenControllerInterface> mock_screen_controller_;

  ScreenDownload screen_download_{
      std::move(mock_recovery_installer_), std::move(mock_update_engine_proxy_),
      mock_draw_interface_, &mock_screen_controller_};
};

TEST_F(ScreenDownloadTest, RepartitionDiskFailed) {
  // Show error screen on repartition failure.
  EXPECT_CALL(*mock_recovery_installer_ptr_, RepartitionDisk())
      .WillOnce(testing::Return(false));
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kGeneralError));
  screen_download_.StartRecovery();
}

TEST_F(ScreenDownloadTest, UpdateEngineError) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::ERROR);
  // Show download error.
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kDownloadError));
  screen_download_.OnProgressChanged(status);
}

TEST_F(ScreenDownloadTest, UpdateEngineProgressComplete) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::UPDATED_NEED_REBOOT);

  EXPECT_CALL(*mock_update_engine_ptr_, TriggerReboot());
  screen_download_.OnProgressChanged(status);
  // Freeze UI, nothing left to do but reboot.
  EXPECT_FALSE(screen_download_.display_update_engine_state_);
}

TEST_F(ScreenDownloadTest, IdleError) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::FINALIZING);
  screen_download_.OnProgressChanged(status);

  // If it changes to `IDLE` from an incorrect state it is an error.
  status.set_current_operation(update_engine::Operation::IDLE);
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kDownloadError));
  screen_download_.OnProgressChanged(status);
}

TEST_F(ScreenDownloadTest, StartUpdateFailed) {
  // Show error screen on update engine failure.
  EXPECT_CALL(*mock_recovery_installer_ptr_, RepartitionDisk())
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*mock_update_engine_ptr_, StartUpdate())
      .WillOnce(testing::Return(false));
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kDownloadError));
  screen_download_.StartRecovery();
}

TEST_F(ScreenDownloadTest, ShowUpdateProgress) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  double test_progress = 0.6;
  status.set_progress(test_progress);
  status.set_current_operation(update_engine::Operation::DOWNLOADING);

  EXPECT_CALL(*mock_recovery_installer_ptr_, RepartitionDisk())
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*mock_update_engine_ptr_, StartUpdate())
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*mock_draw_interface_ptr_, ShowProgressPercentage(::testing::_));
  screen_download_.OnProgressChanged(status);
}

}  // namespace minios
