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

#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>

#include "minios/draw_utils.h"
#include "minios/key_reader.h"
#include "minios/network_manager_interface.h"
#include "minios/recovery_installer.h"
#include "minios/update_engine_proxy.h"

namespace minios {

// Screens contains the different MiniOs Screens as well as specific components
// which are built using the pieces of `DrawUtils`.

class Screens : public DrawUtils,
                public KeyReader::Delegate,
                public UpdateEngineProxy::UpdaterDelegate,
                public NetworkManagerInterface::Observer {
 public:
  explicit Screens(
      ProcessManagerInterface* process_manager,
      std::unique_ptr<RecoveryInstallerInterface> recovery_installer,
      std::shared_ptr<NetworkManagerInterface> network_manager,
      std::shared_ptr<UpdateEngineProxy> update_engine_proxy)
      : DrawUtils(process_manager),
        recovery_installer_(std::move(recovery_installer)),
        network_manager_(network_manager),
        update_engine_proxy_(update_engine_proxy),
        key_states_(kFdsMax, std::vector<bool>(kKeyMax, false)),
        weak_ptr_factory_(this) {
    key_reader_.SetDelegate(this);
  }
  virtual ~Screens() = default;
  // Not copyable or movable.
  Screens(const Screens&) = delete;
  Screens& operator=(const Screens&) = delete;

  // Loads token constants for screen placement, checks whether locale is read
  // from right to left and whether device is detachable.
  bool Init() override;

  // Has the minimum needed to set up tests, to reduce excessive logging. All
  // other components are tested separately.
  bool InitForTest();

  // Shows the MiniOs Screens. Users can navigate between then using up/down
  // arrow keys.
  void StartMiniOsFlow();

  // Waits for key input and repaints the screen with a changed language
  // selection, clears the whole screen including the footer and updates the
  // language dependent constants. Returns to original screen after selection,
  virtual void LanguageMenuOnSelect();

  // Shows a list of all available networks.
  void ShowNetworkDropdown();

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
  FRIEND_TEST(ScreensTestMocks, StartUpdateFailed);

  // Changes the index and enter value based on the given key. Unknown keys are
  // ignored and index is kept within the range of menu items.
  void UpdateButtons(int menu_count, int key, bool* enter);

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

  // After user confirms they want to continue with recovery, begin
  // repartitioning their disk, wiping data, and calling update engine.
  void OnUserPermission();

  // Calls `GetNetworks` to update the the list of networks.
  virtual void UpdateNetworkList();

  // Calls the show screen function of `current_screen`.
  virtual void ShowNewScreen();

  // Shows the buttons of MiniOs screens. Index changes button focus based on
  // button order.
  void ShowMiniOsWelcomeScreen();
  void ShowMiniOsNetworkDropdownScreen();
  void ShowMiniOsGetPasswordScreen();
  void ShowWaitingForConnectionScreen();
  void ShowUserPermissionScreen();
  void ShowMiniOsDownloadingScreen();
  virtual void ShowMiniOsCompleteScreen();
  void ShowMiniOsDebugOptionsScreen();
  void ShowMiniOsLogScreen();

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

  std::unique_ptr<RecoveryInstallerInterface> recovery_installer_;

  KeyReader key_reader_ = KeyReader(/*include_usb=*/true);

  std::shared_ptr<NetworkManagerInterface> network_manager_;

  std::shared_ptr<UpdateEngineProxy> update_engine_proxy_;

  // List of currently available networks.
  std::vector<std::string> network_list_;

  // The networks the user has picked from the menu.
  std::string chosen_network_;

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
      {ScreenType::kUserPermissionScreen, 3},
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

  base::WeakPtrFactory<Screens> weak_ptr_factory_;
};

}  // namespace minios

#endif  // MINIOS_SCREENS_H_
