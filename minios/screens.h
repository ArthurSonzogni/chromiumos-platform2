// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_H_
#define MINIOS_SCREENS_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest_prod.h>

#include "minios/draw_utils.h"
#include "minios/key_reader.h"
#include "minios/network_manager_interface.h"
#include "minios/process_manager.h"
#include "minios/recovery_installer.h"
#include "minios/update_engine_proxy.h"

namespace minios {

extern const char kScreens[];

// Dropdown Menu Colors.
extern const char kMenuDropdownFrameNavy[];
extern const char kMenuDropdownBackgroundBlack[];

// Key values.
extern const int kKeyUp;
extern const int kKeyDown;
extern const int kKeyEnter;
extern const int kKeyVolUp;
extern const int kKeyVolDown;
extern const int kKeyPower;

// Key state parameters.
extern const int kFdsMax;
extern const int kKeyMax;

// All the different screens in the MiniOs Flow. `kDownloadError` is shown when
// there is an Update Engine failure, `kNetworkError` is shown when there is an
// issue getting the networks. `kPasswordError` and `kConnectionError` are shown
// upon failures connecting to a chosen network.
enum class ScreenType {
  kWelcomeScreen = 0,
  kNetworkDropDownScreen = 1,
  kExpandedNetworkDropDownScreen = 2,
  kPasswordScreen = 3,
  kLanguageDropDownScreen = 4,
  kWaitForConnection = 5,
  kStartDownload = 6,
  kDownloadError = 7,
  kNetworkError = 8,
  kPasswordError = 9,
  kConnectionError = 10,
  kGeneralError = 11,
  kDebugOptionsScreen = 12,
  kLogScreen = 13,
};

// Screens contains the different MiniOs Screens as well as specific components
// such as dropdowns and footers which are built using the pieces of
// ScreenBase.

class Screens : public ScreenBase,
                public KeyReader::Delegate,
                public UpdateEngineProxy::UpdaterDelegate,
                public NetworkManagerInterface::Observer {
 public:
  explicit Screens(
      ProcessManagerInterface* process_manager,
      std::unique_ptr<RecoveryInstallerInterface> recovery_installer,
      std::shared_ptr<NetworkManagerInterface> network_manager,
      std::shared_ptr<UpdateEngineProxy> update_engine_proxy)
      : process_manager_(process_manager),
        recovery_installer_(std::move(recovery_installer)),
        network_manager_(network_manager),
        update_engine_proxy_(update_engine_proxy),
        key_states_(kFdsMax, std::vector<bool>(kKeyMax, false)) {
    key_reader_.SetDelegate(this);
  }
  virtual ~Screens() = default;
  // Not copyable or movable.
  Screens(const Screens&) = delete;
  Screens& operator=(const Screens&) = delete;

  // Loads token constants for screen placement, checks whether locale is read
  // from right to left and whether device is detachable.
  bool Init();

  // Has the minimum needed to set up tests, to reduce excessive logging. All
  // other components are tested separately.
  bool InitForTest();

  // Shows the MiniOs Screens. Users can navigate between then using up/down
  // arrow keys.
  void StartMiniOsFlow();

  // Shows the list of all supported locales with the currently selected index
  // highlighted blue. Users can 'scroll' using the up and down arrow keys.
  void ShowLanguageDropdown();

  // Waits for key input and repaints the screen with a changed language
  // selection, clears the whole screen including the footer and updates the
  // language dependent constants. Returns to original screen after selection,
  virtual void LanguageMenuOnSelect();

  // Shows language menu drop down button on base screen. Button is highlighted
  // if it is currently selected.
  void ShowLanguageMenu(bool is_selected);

  // Shows footer with basic instructions and chromebook model.
  void ShowFooter();

  // Clears screen and shows footer and language drop down menu.
  void MessageBaseScreen();

  // Shows a list of all available networks.
  void ShowNetworkDropdown();

  // Shows network menu drop down button on the screen. Button is
  // highlighted if it is currently selected. Selecting this button directs to
  // the expanded network dropdown.
  void ShowCollapsedNetworkDropDown(bool is_selected);

  // Queries list of available networks and shows them as a drop down. On
  // selection sets the 'chosen_network' and redirects to the password
  // screen.
  void ExpandNetworkDropdown();

  // Get user password using the keyboard layout stored in locale. Users can use
  // the tab key to toggle showing the password.
  virtual void GetPassword();

  // Controls the flow of MiniOs by changing screen based on the current index
  // and screen and whether or not a button has been selected(entered). Called
  // every time a valid key press is recorded.
  void SwitchScreen(bool enter);

  // Getter and setter test functions for `index_` and `current_screen`.
  void SetIndexForTest(int index) { index_ = index; }
  int GetIndexForTest() { return index_; }
  void SetScreenForTest(ScreenType current_screen) {
    current_screen_ = current_screen;
  }
  ScreenType GetScreenForTest() { return current_screen_; }

  // Sets network list for test.
  void SetNetworkListForTest_(const std::vector<std::string>& networks) {
    network_list_ = networks;
  }

 private:
  FRIEND_TEST(ScreensTest, ReadDimension);
  FRIEND_TEST(ScreensTest, GetDimension);
  FRIEND_TEST(ScreensTest, GetLangConsts);
  FRIEND_TEST(ScreensTest, GetLangConstsError);
  FRIEND_TEST(ScreensTest, UpdateButtons);
  FRIEND_TEST(ScreensTest, UpdateButtonsIsDetachable);
  FRIEND_TEST(ScreensTest, CheckRightToLeft);
  FRIEND_TEST(ScreensTest, CheckDetachable);
  FRIEND_TEST(ScreensTest, GetVpdFromFile);
  FRIEND_TEST(ScreensTest, GetVpdFromCommand);
  FRIEND_TEST(ScreensTest, GetVpdFromDefault);
  FRIEND_TEST(ScreensTest, GetHwidFromCommand);
  FRIEND_TEST(ScreensTest, GetHwidFromDefault);
  FRIEND_TEST(ScreensTest, GetFreconConstFile);
  FRIEND_TEST(ScreensTest, GetFreconConstNoInt);
  FRIEND_TEST(ScreensTest, GetFreconConstNoFile);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardNoFile);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardNotDict);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardNoKeyboard);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardBadKeyboardFormat);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboard);
  FRIEND_TEST(ScreensTestMocks, OnKeyPress);
  FRIEND_TEST(ScreensTestMocks, UpdateEngineError);
  FRIEND_TEST(ScreensTestMocks, UpdateEngineProgressComplete);
  FRIEND_TEST(ScreensTestMocks, IdleError);
  FRIEND_TEST(ScreensTestMocks, GetNetworks);
  FRIEND_TEST(ScreensTestMocks, OnConnectError);
  FRIEND_TEST(ScreensTestMocks, OnPasswordError);
  FRIEND_TEST(ScreensTestMocks, NoNetworksGiven);
  FRIEND_TEST(ScreensTestMocks, ScreenFlowForwardWithNetwork);
  FRIEND_TEST(ScreensTestMocks, GetNetworksRefresh);
  FRIEND_TEST(ScreensTestMocks, ChangeErrorScreen);
  FRIEND_TEST(ScreensTestMocks, ErrorScreenFallBack);
  FRIEND_TEST(ScreensTestMocks, RepartitionDisk);
  FRIEND_TEST(ScreensTestMocks, RepartitionDiskFailed);
  FRIEND_TEST(ScreensTestMocks, LogScreenNoScreenRefresh);
  FRIEND_TEST(ScreensTestMocks, LogScreenPageDownAndUps);

  // Changes the index and enter value based on the given key. Unknown keys are
  // ignored and index is kept within the range of menu items.
  void UpdateButtons(int menu_count, int key, bool* enter);

  // Read the language constants into memory. Does not change
  // based on the current locale. Returns false on failure.
  bool ReadLangConstants();

  // Sets the width of language token for a given locale. Returns false on
  // error.
  bool GetLangConstants(const std::string& locale, int* lang_width);

  // This function overloads Delegate. It is only called when the key is valid
  // and updates the key state for the given fd and key. Calls `SwitchState` to
  // update the flow once key is recorded as being pressed and released.
  void OnKeyPress(int fd_index, int key_changed, bool key_released) override;

  // `NetworkManagerInterface::Observer` overrides.
  // Updates the list of networks stored by the UI to show in the drop down.
  void OnGetNetworks(const std::vector<std::string>& networks,
                     brillo::Error* error) override;
  // Attempts to connect, shows error screen on failure.
  void OnConnect(const std::string& ssid, brillo::Error* error) override;

  // Calls `GetNetworks` to update the the list of networks.
  virtual void UpdateNetworkList();

  // Does all the reloading needed when the locale is changed, including
  // repainting the screen. Called after `LanguageDropdown` is done.
  virtual void OnLocaleChange();

  // Calls the show screen function of `current_screen`.
  virtual void ShowNewScreen();

  // Shows the buttons of MiniOs screens. Index changes button focus based on
  // button order.
  void ShowMiniOsWelcomeScreen();
  void ShowMiniOsNetworkDropdownScreen();
  void ShowMiniOsGetPasswordScreen();
  void ShowWaitingForConnectionScreen();
  void ShowMiniOsDownloadingScreen();
  virtual void ShowMiniOsCompleteScreen();
  void ShowMiniOsDebugOptionsScreen();
  void ShowMiniOsLogScreen();

  // Checks whether the current language is read from right to left. Must be
  // updated every time the language changes.
  void CheckRightToLeft();

  // Checks whether device has a detachable keyboard and sets `is_detachable`.
  void CheckDetachable();

  // Get region from VPD. Set vpd_region_ to US as default.
  void GetVpdRegion();

  // Get hardware Id from crossystem. Set hwid to `CHROMEBOOK` as default.
  void ReadHardwareId();

  // Get XKB keyboard layout based on the VPD region. Return false on error.
  bool MapRegionToKeyboard(std::string* xkb_keyboard_layout);

  // Calls corresponding MiniOs screen based on update engine status. If UE is
  // `DOWNLOADING` then shows a progress bar with percentage.
  void OnProgressChanged(const update_engine::StatusResult& status) override;

  // Calls error screen components with different messages.
  void ShowErrorScreen(std::string error_message);

  // Reset and show error screen.
  void ChangeToErrorScreen(enum ScreenType error_screen);

  // Updates related to the log screen.
  void UpdateLogScreenButtons();
  void UpdateLogArea();

  ProcessManagerInterface* process_manager_;

  std::unique_ptr<RecoveryInstallerInterface> recovery_installer_;

  KeyReader key_reader_ = KeyReader(/*include_usb=*/true);

  std::shared_ptr<NetworkManagerInterface> network_manager_;

  std::shared_ptr<UpdateEngineProxy> update_engine_proxy_;

  // Whether the device has a detachable keyboard.
  bool is_detachable_{false};

  // Key value pairs that store language widths.
  base::StringPairs lang_constants_;

  // List of all supported locales.
  std::vector<std::string> supported_locales_;

  // List of currently available networks.
  std::vector<std::string> network_list_;

  // The networks the user has picked from the menu.
  std::string chosen_network_;

  // Hardware Id read from crossystem.
  std::string hwid_;

  // Region code read from VPD. Used to determine keyboard layout. Does not
  // change based on selected locale.
  std::string vpd_region_;

  // Records the key press for each fd and key, where the index of the fd is the
  // row and the key code the column. Resets to false after key is released.
  // Only tracks the valid keys.
  std::vector<std::vector<bool>> key_states_;

  // The number of menu buttons on each screen corresponding to the enum
  // numbers, used to keep the index in bounds. The drop down screen counts are
  // updated later based on the locale and network lists.
  std::unordered_map<ScreenType, int> menu_count_{
      {ScreenType::kWelcomeScreen, 3},
      {ScreenType::kNetworkDropDownScreen, 3},
      {ScreenType::kExpandedNetworkDropDownScreen, 0},
      {ScreenType::kPasswordScreen, 3},
      {ScreenType::kLanguageDropDownScreen, 0},
      {ScreenType::kWaitForConnection, 0},
      {ScreenType::kStartDownload, 0},
      {ScreenType::kDownloadError, 3},
      {ScreenType::kNetworkError, 3},
      {ScreenType::kPasswordError, 3},
      {ScreenType::kConnectionError, 3},
      {ScreenType::kGeneralError, 3},
      {ScreenType::kDebugOptionsScreen, 3},
      {ScreenType::kLogScreen, 4}};

  ScreenType current_screen_{ScreenType::kWelcomeScreen};
  // Previous screen only used when changing the language so you know what
  // screen to return to after selection.
  ScreenType previous_screen_{ScreenType::kWelcomeScreen};

  // The `index_` shows which button is highlighted in the `current_screen_`,
  // uses menu_count of current screen to stay in bounds.
  int index_{1};

  // Determines whether we want to display the update engine state changes to
  // the UI. Only necessary after user has entered their password and connected
  // to the network.
  bool display_update_engine_state_{false};

  // Used to keep track of the last seen Update Engine stage to prevent
  // unnecessary screen changes.
  update_engine::Operation previous_update_state_{
      update_engine::Operation::IDLE};

  // Used to keep track of the log to display.
  base::FilePath log_path_;
  // Used to keep track of the byte offsets in the file.
  size_t log_offset_idx_ = 0;
  std::vector<int64_t> log_offsets_ = {0};
};

}  // namespace minios

#endif  // MINIOS_SCREENS_H_
