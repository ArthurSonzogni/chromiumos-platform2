// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_download.h"

#include <memory>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <minios/proto_bindings/minios.pb.h>

#include "minios/log_store_manager.h"
#include "minios/mock_draw.h"
#include "minios/mock_log_store_manager.h"
#include "minios/mock_metrics_reporter.h"
#include "minios/mock_process_manager.h"
#include "minios/mock_recovery_installer.h"
#include "minios/mock_screen_controller.h"
#include "minios/mock_update_engine_proxy.h"
#include "minios/test_utils.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
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

  std::shared_ptr<MockDraw> mock_draw_ = std::make_shared<NiceMock<MockDraw>>();
  MockDraw* mock_draw_ptr_ = mock_draw_.get();

  std::unique_ptr<MockMetricsReporter> mock_metrics_reporter_ =
      std::make_unique<MockMetricsReporter>();
  MockMetricsReporter* mock_metrics_reporter_ptr_ =
      mock_metrics_reporter_.get();

  StrictMock<MockScreenControllerInterface> mock_screen_controller_;
  std::shared_ptr<MockProcessManager> process_manager_ =
      std::make_shared<NiceMock<MockProcessManager>>();

  std::shared_ptr<MockLogStoreManager> mock_log_store_manager =
      std::make_shared<MockLogStoreManager>();

  ScreenDownload screen_download_{std::move(mock_recovery_installer_),
                                  std::move(mock_update_engine_proxy_),
                                  mock_draw_,
                                  std::move(mock_metrics_reporter_),
                                  mock_log_store_manager,
                                  process_manager_,
                                  &mock_screen_controller_};
};

TEST_F(ScreenDownloadTest, RepartitionDiskFailed) {
  // Show error screen on repartition failure.
  EXPECT_CALL(*mock_recovery_installer_ptr_, RepartitionDisk())
      .WillOnce(testing::Return(false));
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kGeneralError));
  EXPECT_CALL(*mock_metrics_reporter_ptr_, RecordNBRStart);
  EXPECT_CALL(*mock_metrics_reporter_ptr_, ReportNBRComplete).Times(0);
  screen_download_.StartRecovery();
}

TEST_F(ScreenDownloadTest, UpdateEngineError) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::ERROR);
  // Show download error.
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kDownloadError));
  EXPECT_CALL(*mock_log_store_manager,
              SaveLogs(LogStoreManager::LogDirection::Disk, _))
      .WillOnce(testing::Return(true));
  screen_download_.OnProgressChanged(status);
}

TEST_F(ScreenDownloadTest, UpdateEngineProgressComplete) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::UPDATED_NEED_REBOOT);
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  screen_download_.log_store_path_ = temp_dir.GetPath();

  EXPECT_CALL(*process_manager_, RunCommand(_, _)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_metrics_reporter_ptr_, ReportNBRComplete);
  EXPECT_CALL(*mock_log_store_manager,
              SaveLogs(LogStoreManager::LogDirection::Stateful, _));
  EXPECT_CALL(mock_screen_controller_,
              OnStateChanged(CheckState(State::COMPLETED)));
  EXPECT_CALL(*mock_update_engine_ptr_, TriggerReboot());
  screen_download_.OnProgressChanged(status);
  // Freeze UI, nothing left to do but reboot.
  EXPECT_FALSE(screen_download_.display_update_engine_state_);
  EXPECT_EQ(State::COMPLETED, screen_download_.GetState().state());
}

TEST_F(ScreenDownloadTest, IdleError) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::FINALIZING);
  EXPECT_CALL(mock_screen_controller_,
              OnStateChanged(CheckState(State::FINALIZING)));
  screen_download_.OnProgressChanged(status);

  // If it changes to `IDLE` from an incorrect state it is an error.
  status.set_current_operation(update_engine::Operation::IDLE);
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kDownloadError));
  screen_download_.OnProgressChanged(status);
}

TEST_F(ScreenDownloadTest, CheckingForUpdateToIdleError) {
  screen_download_.SetDisplayUpdateEngineStateForTest(true);
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::CHECKING_FOR_UPDATE);
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
  EXPECT_CALL(*mock_metrics_reporter_ptr_, RecordNBRStart);
  EXPECT_CALL(*mock_metrics_reporter_ptr_, ReportNBRComplete).Times(0);
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
  EXPECT_CALL(*mock_draw_ptr_, ShowProgressPercentage(::testing::_));
  EXPECT_CALL(mock_screen_controller_,
              OnStateChanged(CheckState(State::RECOVERING)));
  screen_download_.OnProgressChanged(status);
  EXPECT_EQ(State::RECOVERING, screen_download_.GetState().state());
}

TEST_F(ScreenDownloadTest, MoveForward) {
  EXPECT_FALSE(screen_download_.MoveForward(nullptr));
}

TEST_F(ScreenDownloadTest, MoveBackward) {
  EXPECT_FALSE(screen_download_.MoveBackward(nullptr));
}

}  // namespace minios
