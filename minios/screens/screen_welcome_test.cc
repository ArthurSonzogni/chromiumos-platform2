// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_welcome.h"

#include <gtest/gtest.h>

#include "minios/mock_draw.h"
#include "minios/mock_screen_controller.h"

using ::testing::NiceMock;
using ::testing::StrictMock;

namespace minios {

class ScreenWelcomeTest : public ::testing::Test {
 protected:
  std::shared_ptr<MockDraw> mock_draw_ =
      std::make_shared<NiceMock<MockDraw>>();
  MockDraw* mock_draw_ptr_ = mock_draw_.get();
  StrictMock<MockScreenControllerInterface> mock_screen_controller_;

  ScreenWelcome screen_welcome_{mock_draw_, &mock_screen_controller_};
};

TEST_F(ScreenWelcomeTest, GetState) {
  EXPECT_CALL(mock_screen_controller_, OnStateChanged);
  screen_welcome_.Show();
  EXPECT_EQ(State::IDLE, screen_welcome_.GetState().state());
}

TEST_F(ScreenWelcomeTest, MoveForward) {
  EXPECT_CALL(mock_screen_controller_, OnForward(&screen_welcome_));
  EXPECT_TRUE(screen_welcome_.MoveForward(nullptr));
}

TEST_F(ScreenWelcomeTest, MoveBackward) {
  EXPECT_FALSE(screen_welcome_.MoveBackward(nullptr));
}

}  // namespace minios
