// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "minios/draw_interface.h"
#include "minios/mock_draw_interface.h"
#include "minios/mock_network_manager.h"
#include "minios/mock_process_manager.h"
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
  std::shared_ptr<NetworkManagerInterface> mock_network_manager_ =
      std::make_shared<NiceMock<MockNetworkManager>>();
  MockProcessManager process_manager_;
  ScreenController screen_controller_{draw_interface_, nullptr,
                                      mock_network_manager_, &process_manager_};
};

TEST_F(ScreenControllerTest, OnForward) {
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kWelcomeScreen));
  screen_controller_.OnForward(&mock_screen_);
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreen());

  // Go ask for user permission after getting network info.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kNetworkDropDownScreen));
  screen_controller_.OnForward(&mock_screen_);
  EXPECT_EQ(ScreenType::kUserPermissionScreen,
            screen_controller_.GetCurrentScreen());

  // Forward from error screen goes to debug options.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kGeneralError));
  screen_controller_.OnForward(&mock_screen_);
  EXPECT_EQ(ScreenType::kDebugOptionsScreen,
            screen_controller_.GetCurrentScreen());

  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kDebugOptionsScreen));
  screen_controller_.OnForward(&mock_screen_);
  EXPECT_EQ(ScreenType::kLogScreen, screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, OnBackward) {
  // Backward from log screen goes to debug options.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kLogScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kDebugOptionsScreen,
            screen_controller_.GetCurrentScreen());

  // Permission denied goes back to the start screen.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kUserPermissionScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kWelcomeScreen, screen_controller_.GetCurrentScreen());

  screen_controller_.SetCurrentScreenForTest(
      ScreenType::kNetworkDropDownScreen);
  // Password screen goes back to the first network screen.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kPasswordError));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreen());

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

TEST_F(ScreenControllerTest, CancelChangeLocale) {
  screen_controller_.SetCurrentScreenForTest(
      ScreenType::kNetworkDropDownScreen);

  screen_controller_.SwitchLocale(&mock_screen_);
  EXPECT_EQ(ScreenType::kLanguageDropDownScreen,
            screen_controller_.GetCurrentScreen());

  // Cancel language dropdown selection, return to original screen.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kLanguageDropDownScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, NullScreenCancelChangeLocale) {
  screen_controller_.SwitchLocale(nullptr);
  EXPECT_EQ(ScreenType::kLanguageDropDownScreen,
            screen_controller_.GetCurrentScreen());

  // Cancel language dropdown selection, return to welcome screen.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kLanguageDropDownScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kWelcomeScreen, screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, RestartFromDownloadError) {
  // Starting from Download error screen.
  screen_controller_.SetCurrentScreenForTest(ScreenType::kDownloadError);
  EXPECT_EQ(ScreenType::kDownloadError, screen_controller_.GetCurrentScreen());

  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kDownloadError));
  screen_controller_.OnBackward(&mock_screen_);

  // Back to start screen.
  EXPECT_EQ(ScreenType::kWelcomeScreen, screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, RestartFromNetworkError) {
  // Starting from network error screen.
  screen_controller_.SetCurrentScreenForTest(ScreenType::kNetworkError);

  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kNetworkError));
  screen_controller_.OnBackward(&mock_screen_);

  // Back to dropdown.
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, RestartFromPasswordError) {
  // Start from password error screen.
  screen_controller_.SetCurrentScreenForTest(
      ScreenType::kNetworkDropDownScreen);
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kPasswordError));
  screen_controller_.OnBackward(&mock_screen_);

  // Back to dropdown.
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, DebugOptionsToError) {
  // Going back from debug options should return to the error screen it was
  // called from.
  screen_controller_.SetCurrentScreenForTest(ScreenType::kNetworkError);
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kNetworkError));
  screen_controller_.OnForward(&mock_screen_);
  EXPECT_EQ(ScreenType::kDebugOptionsScreen,
            screen_controller_.GetCurrentScreen());

  // Go to log screen.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kDebugOptionsScreen));
  screen_controller_.OnForward(&mock_screen_);
  EXPECT_EQ(ScreenType::kLogScreen, screen_controller_.GetCurrentScreen());

  // Go back all the way to the original error screen.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kLogScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kDebugOptionsScreen,
            screen_controller_.GetCurrentScreen());

  // Back to `kNetworkError`.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kDebugOptionsScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kNetworkError, screen_controller_.GetCurrentScreen());
}

TEST_F(ScreenControllerTest, DebugOptionsBackInvalid) {
  // Going back from debug options should return to the error screen it was
  // called from, but if there is no valid error screen, go back to the
  // beginning.
  EXPECT_CALL(mock_screen_, GetType())
      .WillOnce(testing::Return(ScreenType::kDebugOptionsScreen));
  screen_controller_.OnBackward(&mock_screen_);
  EXPECT_EQ(ScreenType::kWelcomeScreen, screen_controller_.GetCurrentScreen());
}

}  // namespace minios
