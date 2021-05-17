// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "minios/draw_interface.h"
#include "minios/mock_draw_interface.h"
#include "minios/mock_screen_interface.h"
#include "minios/screen_controller.h"

using ::testing::NiceMock;

namespace minios {

class ScreenControllerTest : public ::testing::Test {
 public:
  void SetUp() override {
    screen_controller_.SetCurrentScreenForTest(ScreenType::kWelcomeScreen);
  }

 protected:
  std::shared_ptr<DrawInterface> draw_interface_ =
      std::make_shared<NiceMock<MockDrawInterface>>();
  MockScreenInterface mock_screen_;
  ScreenController screen_controller_{draw_interface_, nullptr};
};

TEST_F(ScreenControllerTest, OnForward) {
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kWelcomeScreen));
  screen_controller_.OnForward(&mock_screen_);
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, OnBackward) {
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kNetworkDropDownScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kWelcomeScreen, screen_controller_.GetCurrentScreen());

  // Cannot go back from `kWelcomeScreen'.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kWelcomeScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kWelcomeScreen, screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, ChangeLocale) {
  screen_controller_.SetCurrentScreenForTest(
      ScreenType::kNetworkDropDownScreen);

  screen_controller_.SwitchLocale(&mock_screen_);
  EXPECT_EQ(ScreenType::kLanguageDropDownScreen,
            screen_controller_.GetCurrentScreen());

  // Return from language dropdown, return to original screen.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kLanguageDropDownScreen));
  screen_controller_.UpdateLocale(&mock_screen_, /*index=*/1);
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreen());
}

}  // namespace minios
