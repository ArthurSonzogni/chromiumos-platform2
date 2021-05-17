// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_NETWORK_H_
#define MINIOS_SCREEN_NETWORK_H_

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>

#include "minios/network_manager_interface.h"
#include "minios/screen_base.h"

namespace minios {

class ScreenNetwork : public ScreenBase,
                      public NetworkManagerInterface::Observer {
 public:
  ScreenNetwork(std::shared_ptr<DrawInterface> draw_utils,
                std::shared_ptr<NetworkManagerInterface> network_manager,
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
  // Attempts to connect, shows error screen on failure. Reference
  // ScreenPassword for implementation.
  void OnConnect(const std::string& ssid, brillo::Error* error) override{};

  void SetIndexForTest(int index) { index_ = index; }

 private:
  // Returns true if `screen_type_` is `kExpandedNetworkDropDownScreen`, false
  // otherwise.
  bool IsDropDownOpen();

  // Updates network dropdown menu with current selection.
  void UpdateMenu();

  // Updates buttons with current selection.
  void ShowButtons();

  // Shows network menu drop down button on the screen. Button is
  // highlighted if it is currently selected. Selecting this button directs to
  // the expanded network dropdown.
  void ShowCollapsedNetworkDropDown(bool is_selected);

  // Shows a list of all available networks.
  void ShowNetworkDropdown(int current_index);

  std::shared_ptr<NetworkManagerInterface> network_manager_;

  std::vector<std::string> networks_;

  // The network the user has chosen.
  std::string chosen_network_;

  // Screen type is 'kExpandedNetworkDropDownScreen' if the dropdown is selected
  // and open or `kNetworkDropDownScreen` when it is closed.
  ScreenType screen_type_;
};

}  // namespace minios

#endif  // MINIOS_SCREEN_NETWORK_H_
