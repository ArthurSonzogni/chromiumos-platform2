// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "minios/draw_interface.h"
#include "minios/mock_draw_interface.h"
#include "minios/screen_controller.h"

using ::testing::NiceMock;

namespace minios {

class ScreenControllerTest : public ::testing::Test {
 protected:
  std::shared_ptr<DrawInterface> draw_interface_ =
      std::make_shared<NiceMock<MockDrawInterface>>();
  ScreenController screen_controller_{draw_interface_};
};

TEST_F(ScreenControllerTest, OnForward) {
  screen_controller_.OnForward(
      screen_controller_.GetCurrentScreenPtrForTest(ScreenType::kWelcomeScreen)
          .get());
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreenForTest());
}

TEST_F(ScreenControllerTest, OnBackward) {
  screen_controller_.OnBackward(
      screen_controller_
          .GetCurrentScreenPtrForTest(ScreenType::kNetworkDropDownScreen)
          .get());
  EXPECT_EQ(ScreenType::kWelcomeScreen,
            screen_controller_.GetCurrentScreenForTest());

  // Cannot go back from `kWelcomeScreen'.
  screen_controller_.OnBackward(
      screen_controller_.GetCurrentScreenPtrForTest(ScreenType::kWelcomeScreen)
          .get());
  EXPECT_EQ(ScreenType::kWelcomeScreen,
            screen_controller_.GetCurrentScreenForTest());
}

TEST_F(ScreenControllerTest, ChangeLocale) {
  screen_controller_.SetCurrentScreenForTest(
      ScreenType::kNetworkDropDownScreen);

  screen_controller_.SwitchLocale(
      screen_controller_
          .GetCurrentScreenPtrForTest(ScreenType::kNetworkDropDownScreen)
          .get());
  EXPECT_EQ(ScreenType::kLanguageDropDownScreen,
            screen_controller_.GetCurrentScreenForTest());

  // Return from language dropdown, return to original screen.
  screen_controller_.UpdateLocale(
      screen_controller_
          .GetCurrentScreenPtrForTest(ScreenType::kLanguageDropDownScreen)
          .get(),
      /*index=*/1);
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreenForTest());
}

}  // namespace minios
