// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <linux/input-event-codes.h>
#include <memory>

#include "minios/mock_draw_interface.h"
#include "minios/mock_log_store_manager.h"
#include "minios/mock_process_manager.h"
#include "minios/mock_screen_controller.h"
#include "minios/screens/screen_debug_options.h"

using ::testing::_;
using ::testing::Contains;
using ::testing::NiceMock;
using ::testing::StrictMock;

namespace minios {

class ScreenDebugOptionsTest : public ::testing::Test {
 protected:
  std::shared_ptr<MockDrawInterface> mock_draw_interface_ =
      std::make_shared<NiceMock<MockDrawInterface>>();
  MockDrawInterface* mock_draw_interface_ptr_ = mock_draw_interface_.get();
  StrictMock<MockScreenControllerInterface> mock_screen_controller_;
  std::shared_ptr<MockLogStoreManager> mock_log_store_manager =
      std::make_shared<StrictMock<MockLogStoreManager>>();
  std::shared_ptr<MockProcessManager> process_manager =
      std::make_shared<MockProcessManager>();

  ScreenDebugOptions screen_debug_options_{
      mock_draw_interface_, mock_log_store_manager, process_manager,
      &mock_screen_controller_};
};

TEST_F(ScreenDebugOptionsTest, GetState) {
  EXPECT_CALL(mock_screen_controller_, OnStateChanged);
  screen_debug_options_.Show();
  EXPECT_EQ(State::DEBUG_OPTIONS, screen_debug_options_.GetState().state());
}

TEST_F(ScreenDebugOptionsTest, ClearLogs) {
  EXPECT_CALL(*process_manager, RunCommand(_, _)).WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_log_store_manager, ClearLogs())
      .WillOnce(testing::Return(true));
  screen_debug_options_.index_ = 1;
  screen_debug_options_.OnKeyPress(KEY_ENTER);
}

TEST_F(ScreenDebugOptionsTest, MoveForward) {
  EXPECT_CALL(mock_screen_controller_, OnForward(&screen_debug_options_));
  EXPECT_TRUE(screen_debug_options_.MoveForward(nullptr));
}

TEST_F(ScreenDebugOptionsTest, MoveBackward) {
  EXPECT_CALL(mock_screen_controller_, OnBackward(&screen_debug_options_));
  EXPECT_TRUE(screen_debug_options_.MoveBackward(nullptr));
}

}  // namespace minios
