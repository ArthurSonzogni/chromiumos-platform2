// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "minios/mock_process_manager.h"
#include "minios/mock_recovery_installer.h"
#include "minios/mock_update_engine_proxy.h"
#include "minios/screens.h"

using testing::_;

namespace minios {

class ScreensTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_root_ = temp_dir_.GetPath().value();
    screens_.SetRootForTest(test_root_);

    screens_path_ = base::FilePath(test_root_).Append(kScreens);

    base::FilePath locale_dir_en =
        base::FilePath(screens_path_).Append("en-US");
    ASSERT_TRUE(base::CreateDirectory(locale_dir_en));
    base::FilePath locale_dir_fr = base::FilePath(screens_path_).Append("fr");
    ASSERT_TRUE(CreateDirectory(locale_dir_fr));
    // Create and write constants file.
    std::string token_consts =
        "TITLE_minios_token_HEIGHT=38 \nDESC_minios_token_HEIGHT=44\n"
        "DESC_screen_token_HEIGHT=incorrect\nDEBUG_OPTIONS_BTN_WIDTH=99\n";
    ASSERT_TRUE(
        base::WriteFile(locale_dir_en.Append("constants.sh"), token_consts));

    // Create directories.
    ASSERT_TRUE(
        base::CreateDirectory(base::FilePath(test_root_).Append("dev/pts")));
    console_ = base::FilePath(test_root_).Append("dev/pts/0");
    ASSERT_TRUE(base::WriteFile(console_, ""));
    ASSERT_TRUE(CreateDirectory(
        base::FilePath(screens_path_).Append("glyphs").Append("white")));
    ASSERT_TRUE(CreateDirectory(
        base::FilePath(test_root_).Append("sys/firmware/vpd/ro")));
    ASSERT_TRUE(screens_.InitForTest());
  }

 protected:
  // Test directory.
  base::ScopedTempDir temp_dir_;
  // Path to output pts.
  base::FilePath console_;
  // Path to /etc/screens in test directory.
  base::FilePath screens_path_;
  MockProcessManager mock_process_manager_;
  Screens screens_{&mock_process_manager_, nullptr, nullptr, nullptr};
  std::string test_root_;
};

class MockScreens : public Screens {
 public:
  explicit MockScreens(
      std::unique_ptr<MockRecoveryInstaller> mock_recovery_installer,
      std::unique_ptr<MockUpdateEngineProxy> mock_update_engine_proxy)
      : Screens(nullptr,
                std::move(mock_recovery_installer),
                nullptr,
                std::move(mock_update_engine_proxy)) {}
  MOCK_METHOD(bool,
              ShowBox,
              (int offset_x,
               int offset_y,
               int size_x,
               int size_y,
               const std::string& color));
  MOCK_METHOD(bool,
              ShowImage,
              (const base::FilePath& image_name, int offset_x, int offset_y));
  MOCK_METHOD(bool,
              ShowMessage,
              (const std::string& message_token, int offset_x, int offset_y));
  MOCK_METHOD(bool,
              ShowText,
              (const std::string& text,
               int glyph_offset_h,
               int glyph_offset_v,
               const std::string& color));
  MOCK_METHOD(void,
              ShowInstructionsWithTitle,
              (const std::string& message_token));
  MOCK_METHOD(void, ShowNewScreen, ());
  MOCK_METHOD(void, LanguageMenuOnSelect, ());
  MOCK_METHOD(void, GetPassword, ());
  MOCK_METHOD(void, LocaleChange, (int LocaleChange));
  MOCK_METHOD(void, ShowMiniOsCompleteScreen, ());
  MOCK_METHOD(void, UpdateNetworkList, ());
};

class ScreensTestMocks : public ::testing::Test {
 public:
  ScreensTestMocks()
      : mock_recovery_installer_(std::make_unique<MockRecoveryInstaller>()),
        mock_recovery_installer_ptr_(mock_recovery_installer_.get()),
        mock_update_engine_proxy_(std::make_unique<MockUpdateEngineProxy>()),
        mock_update_engine_ptr_(mock_update_engine_proxy_.get()),
        mock_screens_(MockScreens(std::move(mock_recovery_installer_),
                                  std::move(mock_update_engine_proxy_))) {}
  void SetUp() override {
    base::ScopedTempDir temp_dir_;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    screens_path_ = base::FilePath(temp_dir_.GetPath()).Append(kScreens);
    brillo::TouchFile(screens_path_.Append("en-US").Append("constants.sh"));
    mock_screens_.SetRootForTest(temp_dir_.GetPath().value());
    mock_screens_.InitForTest();
  }

 protected:
  base::FilePath screens_path_;
  std::unique_ptr<MockRecoveryInstaller> mock_recovery_installer_;
  MockRecoveryInstaller* mock_recovery_installer_ptr_;
  std::unique_ptr<MockUpdateEngineProxy> mock_update_engine_proxy_;
  MockUpdateEngineProxy* mock_update_engine_ptr_;
  MockScreens mock_screens_;
};

TEST_F(ScreensTestMocks, OnKeyPress) {
  std::vector<int> keys = {kKeyDown, kKeyEnter, kKeyUp};
  mock_screens_.SetIndexForTest(1);
  // Index changes up one after both press and release are recorded in
  // `key_state_`. `SwitchScreen` is called for every valid key release.

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnKeyPress(0, kKeyDown, false);
  EXPECT_EQ(mock_screens_.GetIndexForTest(), 1);
  mock_screens_.OnKeyPress(0, kKeyDown, true);
  EXPECT_EQ(mock_screens_.GetIndexForTest(), 2);

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnKeyPress(0, kKeyEnter, false);
  mock_screens_.OnKeyPress(0, kKeyEnter, true);
}

TEST_F(ScreensTestMocks, ScreenFlowLanguage) {
  // Test making a selection on the language screen and then returning back to
  // the previous screen.
  // Index 0 on a normal screen is the language dropdown button.
  mock_screens_.SetIndexForTest(0);
  mock_screens_.SetScreenForTest(ScreenType::kWelcomeScreen);

  // Calls language menu.
  EXPECT_CALL(mock_screens_, LanguageMenuOnSelect());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kLanguageDropDownScreen,
            mock_screens_.GetScreenForTest());

  // Select language from menu, make changes, and return to previous screen.
  EXPECT_CALL(mock_screens_, LocaleChange(_));
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kWelcomeScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, ScreenFlowForwardWithNetwork) {
  // Test the screen flow forward starting from the welcome screen.
  mock_screens_.SetIndexForTest(1);
  mock_screens_.SetScreenForTest(ScreenType::kWelcomeScreen);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(/*enter=*/false);

  // Screen has not changed since enter is false.
  EXPECT_EQ(ScreenType::kWelcomeScreen, mock_screens_.GetScreenForTest());

  // Moves to next screen in flow. kNetworkDropDownScreen.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            mock_screens_.GetScreenForTest());

  // Enter goes to kExpandedNetworkDropDownScreen.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kExpandedNetworkDropDownScreen,
            mock_screens_.GetScreenForTest());

  // Enter goes to kPasswordScreen.
  mock_screens_.SetIndexForTest(1);
  mock_screens_.SetNetworkListForTest_({"test1", "test2"});
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kPasswordScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, ScreenBackward) {
  // Test the screen flow backward starting from the password screen.
  mock_screens_.SetIndexForTest(2);
  // Start at password screen.
  mock_screens_.SetScreenForTest(ScreenType::kPasswordScreen);

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  // Moves back to kDropDownScreen.
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            mock_screens_.GetScreenForTest());

  // Enter goes back to kWelcomeScreen.
  mock_screens_.SetIndexForTest(2);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kWelcomeScreen, mock_screens_.GetScreenForTest());

  // Cannot go further back from kWelcomeScreen.
  mock_screens_.SetIndexForTest(2);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kWelcomeScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, UpdateEngineError) {
  mock_screens_.display_update_engine_state_ = true;
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::ERROR);

  // Show download error.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnProgressChanged(status);
  EXPECT_FALSE(mock_screens_.display_update_engine_state_);
}

TEST_F(ScreensTestMocks, UpdateEngineProgressComplete) {
  mock_screens_.display_update_engine_state_ = true;
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::UPDATED_NEED_REBOOT);

  EXPECT_CALL(mock_screens_, ShowMiniOsCompleteScreen());
  mock_screens_.OnProgressChanged(status);
  // Freeze UI, nothing left to do but reboot.
  EXPECT_FALSE(mock_screens_.display_update_engine_state_);
}

TEST_F(ScreensTestMocks, IdleError) {
  mock_screens_.display_update_engine_state_ = true;
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::FINALIZING);
  mock_screens_.OnProgressChanged(status);

  // If it changes to `IDLE` from an incorrect state it is an error.
  status.set_current_operation(update_engine::Operation::IDLE);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnProgressChanged(status);
  EXPECT_FALSE(mock_screens_.display_update_engine_state_);
}

TEST_F(ScreensTestMocks, InvalidNetwork) {
  mock_screens_.SetScreenForTest(ScreenType::kExpandedNetworkDropDownScreen);

  // Set list of available networks to empty.
  mock_screens_.SetNetworkListForTest_({"network"});
  mock_screens_.SetIndexForTest(1);

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  // Goes back to the dropdown screen because the network chosen was invalid.
  EXPECT_EQ(ScreenType::kExpandedNetworkDropDownScreen,
            mock_screens_.GetScreenForTest());

  mock_screens_.SetNetworkListForTest_({"test1"});
  mock_screens_.SetIndexForTest(5);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  // Goes back to the dropdown screen because the network chosen was invalid.
  EXPECT_EQ(ScreenType::kExpandedNetworkDropDownScreen,
            mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, RestartFromDownloadError) {
  // Starting from Download error screen.
  mock_screens_.SetScreenForTest(ScreenType::kDownloadError);
  mock_screens_.SetIndexForTest(1);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  // Back to start screen.
  EXPECT_EQ(ScreenType::kWelcomeScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, RestartFromNetworkError) {
  // Starting from network error screen.
  mock_screens_.SetScreenForTest(ScreenType::kNetworkError);
  mock_screens_.SetIndexForTest(1);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  // Back to dropdown.
  EXPECT_EQ(ScreenType::kNetworkDropDownScreen,
            mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, GetNetworks) {
  mock_screens_.OnGetNetworks({"test1", "test2", "test3"}, nullptr);
  // Menu count is updated.
  EXPECT_EQ(
      4, mock_screens_.menu_count_[ScreenType::kExpandedNetworkDropDownScreen]);

  // Network error.
  brillo::ErrorPtr error_ptr =
      brillo::Error::Create(FROM_HERE, "HTTP", "404", "Not found", nullptr);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  // Reset and show error screen.
  mock_screens_.OnGetNetworks({}, error_ptr.get());
  // One in the menu count for the back button.
  EXPECT_EQ(
      1, mock_screens_.menu_count_[ScreenType::kExpandedNetworkDropDownScreen]);
  EXPECT_EQ(0, mock_screens_.network_list_.size());
  EXPECT_EQ(ScreenType::kNetworkError, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, GetNetworksRefresh) {
  mock_screens_.SetScreenForTest(ScreenType::kExpandedNetworkDropDownScreen);
  EXPECT_TRUE(mock_screens_.network_list_.empty());
  // Menu count is updated amd drop down screen is refreshed.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnGetNetworks({"test1", "test2", "test3"}, nullptr);
  EXPECT_EQ(
      4, mock_screens_.menu_count_[ScreenType::kExpandedNetworkDropDownScreen]);
}

TEST_F(ScreensTestMocks, OnConnectError) {
  mock_screens_.chosen_network_ = "test-ssid";
  // Network error, show corresponding screen.
  brillo::ErrorPtr error_ptr =
      brillo::Error::Create(FROM_HERE, "HTTP", "404", "Not found", nullptr);

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnConnect(mock_screens_.chosen_network_, error_ptr.get());
  EXPECT_EQ(ScreenType::kConnectionError, mock_screens_.GetScreenForTest());
  EXPECT_TRUE(mock_screens_.chosen_network_.empty());
}

TEST_F(ScreensTestMocks, OnPasswordError) {
  mock_screens_.chosen_network_ = "test-ssid";
  // Network error, show corresponding screen.
  brillo::ErrorPtr error_ptr = brillo::Error::Create(
      FROM_HERE, "Password", "org.chromium.flimflam.Error.InvalidPassphrase",
      "Invalid passphrase", nullptr);

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnConnect(mock_screens_.chosen_network_, error_ptr.get());
  EXPECT_EQ(ScreenType::kPasswordError, mock_screens_.GetScreenForTest());
  EXPECT_TRUE(mock_screens_.chosen_network_.empty());
}

TEST_F(ScreensTestMocks, ChangeErrorScreen) {
  mock_screens_.SetScreenForTest(ScreenType::kNetworkDropDownScreen);
  mock_screens_.SetIndexForTest(2);
  mock_screens_.display_update_engine_state_ = true;
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.ChangeToErrorScreen(ScreenType::kNetworkError);

  // Reset state and show error.
  EXPECT_EQ(ScreenType::kNetworkError, mock_screens_.GetScreenForTest());
  EXPECT_EQ(1, mock_screens_.GetIndexForTest());
  EXPECT_FALSE(mock_screens_.display_update_engine_state_);
}

TEST_F(ScreensTestMocks, ErrorScreenFallBack) {
  // Error images not available, fall back to general error screen.
  brillo::TouchFile(
      screens_path_.Append("en-US").Append("title_MiniOS_test_error.png"));
  EXPECT_CALL(mock_screens_, ShowInstructionsWithTitle("MiniOS_general_error"));
  mock_screens_.ShowErrorScreen("MiniOS_test_error");

  // Create both error images to show error.
  brillo::TouchFile(
      screens_path_.Append("en-US").Append("desc_MiniOS_test_error.png"));
  EXPECT_CALL(mock_screens_, ShowInstructionsWithTitle("MiniOS_test_error"));
  mock_screens_.ShowErrorScreen("MiniOS_test_error");
}

TEST_F(ScreensTestMocks, RepartitionDisk) {
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  EXPECT_CALL(*mock_recovery_installer_ptr_, RepartitionDisk())
      .WillOnce(testing::Return(true));
  mock_screens_.OnUserPermission();
}

TEST_F(ScreensTestMocks, RepartitionDiskFailed) {
  // Show error screen on repartition failure.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  EXPECT_CALL(*mock_recovery_installer_ptr_, RepartitionDisk())
      .WillOnce(testing::Return(false));
  mock_screens_.OnUserPermission();
  EXPECT_EQ(ScreenType::kGeneralError, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, ErrorScreenIntoDebugOptionsScreen) {
  for (const auto& screen_type :
       {ScreenType::kDownloadError, ScreenType::kNetworkError,
        ScreenType::kPasswordError, ScreenType::kConnectionError}) {
    mock_screens_.SetScreenForTest(screen_type);
    mock_screens_.SetIndexForTest(2);
    EXPECT_CALL(mock_screens_, ShowNewScreen());
    mock_screens_.SwitchScreen(true);
    EXPECT_EQ(ScreenType::kDebugOptionsScreen,
              mock_screens_.GetScreenForTest());
  }
}

TEST_F(ScreensTestMocks, DebugOptionsScreenBackGoesToWelcome) {
  mock_screens_.SetScreenForTest(ScreenType::kDebugOptionsScreen);
  mock_screens_.SetIndexForTest(2);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kWelcomeScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, DebugOptionsScreenIntoLogScreen) {
  mock_screens_.SetScreenForTest(ScreenType::kDebugOptionsScreen);
  mock_screens_.SetIndexForTest(1);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kLogScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, LogScreenNoScreenRefresh) {
  base::ScopedTempDir tmp_dir_;
  ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
  const base::FilePath& kPath = tmp_dir_.GetPath().Append("file");
  ASSERT_TRUE(base::WriteFile(kPath, "a\nb\nc\n"));
  mock_screens_.log_path_ = kPath;

  // No redraw as already on the correct screen.
  mock_screens_.SetScreenForTest(ScreenType::kLogScreen);
  mock_screens_.SetIndexForTest(1);
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kLogScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, LogScreenPageDownAndUps) {
  base::ScopedTempDir tmp_dir_;
  ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
  const base::FilePath& kPath = tmp_dir_.GetPath().Append("file");
  ASSERT_TRUE(base::WriteFile(kPath, "a\nb\nc\n"));
  mock_screens_.log_path_ = kPath;
  mock_screens_.log_offset_idx_ = 0;
  mock_screens_.log_offsets_ = {0, 3, 5};

  // Act of scrolling up on the log.
  mock_screens_.SetScreenForTest(ScreenType::kLogScreen);
  mock_screens_.SetIndexForTest(1);
  // No redraws should be triggered.
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kLogScreen, mock_screens_.GetScreenForTest());

  // Act of scrolling down on the log.
  mock_screens_.SetIndexForTest(2);
  EXPECT_CALL(
      mock_screens_,
      ShowImage(screens_path_.Append("log_area_border_large.png"), _, _));
  EXPECT_CALL(mock_screens_, ShowText("\nc", _, _, "white"));
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kLogScreen, mock_screens_.GetScreenForTest());

  // Act of scrolling up on the log.
  mock_screens_.SetIndexForTest(1);
  EXPECT_CALL(
      mock_screens_,
      ShowImage(screens_path_.Append("log_area_border_large.png"), _, _));
  EXPECT_CALL(mock_screens_, ShowText("a\nb", _, _, "white"));
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(ScreenType::kLogScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, LogScreenNonEnter) {
  mock_screens_.SetScreenForTest(ScreenType::kLogScreen);
  mock_screens_.SetIndexForTest(1);
  mock_screens_.SwitchScreen(false);
  EXPECT_EQ(ScreenType::kLogScreen, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, StartUpdateFailed) {
  // Show error screen on update engine failure.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  EXPECT_CALL(*mock_recovery_installer_ptr_, RepartitionDisk())
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*mock_update_engine_ptr_, StartUpdate())
      .WillOnce(testing::Return(false));
  mock_screens_.OnUserPermission();
  EXPECT_EQ(ScreenType::kDownloadError, mock_screens_.GetScreenForTest());
}

}  // namespace minios
