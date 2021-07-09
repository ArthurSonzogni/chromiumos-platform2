// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_SCREEN_NETWORK_H_
#define MINIOS_SCREENS_SCREEN_NETWORK_H_

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>

#include "minios/key_reader.h"
#include "minios/network_manager_interface.h"
#include "minios/screens/screen_base.h"

namespace minios {

// The internal states of `ScreenNetwork`.
enum class NetworkState {
  kDropdownClosed = 0,
  kDropdownOpen = 1,
  kGetPassword = 2,
};

class ScreenNetwork : public ScreenBase,
                      public NetworkManagerInterface::Observer {
 public:
  ScreenNetwork(std::shared_ptr<DrawInterface> draw_utils,
                std::shared_ptr<NetworkManagerInterface> network_manager,
                KeyReader* key_reader,
                ScreenControllerInterface* screen_controller);

  ~ScreenNetwork();

  ScreenNetwork(const ScreenNetwork&) = delete;
  ScreenNetwork& operator=(const ScreenNetwork&) = delete;

  void Show() override;
  void Reset() override;
  void OnKeyPress(int key_changed) override;
  ScreenType GetType() override;
  std::string GetName() override;

  // `NetworkManagerInterface::Observer` overrides.
  // Updates the list of networks stored by the UI to show in the drop down.
  void OnGetNetworks(const std::vector<std::string>& networks,
                     brillo::Error* error) override;

  // Attempts to connect, shows error screen on failure.
  void OnConnect(const std::string& ssid, brillo::Error* error) override;

  void SetIndexForTest(int index) { index_ = index; }
  void SetStateForTest(NetworkState state) { state_ = state; }

 private:
  // Updates buttons with current selection.
  void ShowButtons();

  // Get user password using the keyboard layout stored in locale. Users can use
  // the tab key to toggle showing the password.
  void GetPassword();

  // Changes UI with instructions to wait for screen. This screen is
  // automatically changed when `OnConnect` returns.
  void WaitForConnection();

  // Shows network menu drop down button on the screen. Button is
  // highlighted if it is currently selected. Selecting this button directs to
  // the expanded network dropdown.
  void ShowCollapsedNetworkDropDown(bool is_selected);

  // Shows a list of all available networks.
  void ShowNetworkDropdown(int current_index);

  std::shared_ptr<NetworkManagerInterface> network_manager_;

  KeyReader* key_reader_;

  std::vector<std::string> networks_;

  // The network the user has chosen.
  std::string chosen_network_;

  // Number of items in the network dropdown.
  int items_per_page_;

  // Current internal state.
  NetworkState state_;
};

}  // namespace minios

#endif  // MINIOS_SCREENS_SCREEN_NETWORK_H_
