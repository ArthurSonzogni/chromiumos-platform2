// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "minios/key_reader.h"
#include "minios/mock_draw_interface.h"
#include "minios/mock_network_manager.h"
#include "minios/mock_screen_controller.h"
#include "minios/screens/screen_network.h"

using ::testing::NiceMock;

namespace minios {

class ScreenNetworkTest : public ::testing::Test {
 protected:
  std::shared_ptr<NetworkManagerInterface> mock_network_manager_ =
      std::make_shared<NiceMock<MockNetworkManager>>();
  std::shared_ptr<DrawInterface> mock_draw_interface_ =
      std::make_shared<NiceMock<MockDrawInterface>>();
  NiceMock<MockScreenControllerInterface> mock_screen_controller_;
  ScreenNetwork screen_network_{mock_draw_interface_, mock_network_manager_,
                                nullptr, &mock_screen_controller_};
};

TEST_F(ScreenNetworkTest, GetNetworks) {
  screen_network_.OnGetNetworks(
      {{.ssid = "test1"}, {.ssid = "test2"}, {.ssid = "test3"}}, nullptr);

  // Network error.
  brillo::ErrorPtr error_ptr =
      brillo::Error::Create(FROM_HERE, "HTTP", "404", "Not found", nullptr);

  // Reset and show error screen.
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kNetworkError));
  screen_network_.OnGetNetworks({}, error_ptr.get());
  EXPECT_EQ(screen_network_.GetIndexForTest(), 1);
  EXPECT_EQ(screen_network_.GetButtonCountForTest(), 4);
  EXPECT_EQ(screen_network_.GetStateForTest(), NetworkState::kDropdownClosed);
}

TEST_F(ScreenNetworkTest, GetNetworksWithEthernet) {
  // Ethernet included in list of networks.
  screen_network_.SetStateForTest(NetworkState::kDropdownOpen);
  screen_network_.OnGetNetworks({{.ssid = "test1", .strength = 0},
                                 {.ssid = "test2", .strength = 10},
                                 {.ssid = kShillEthernetLabel, .strength = 2},
                                 {.ssid = "test3", .strength = 7}},
                                nullptr);

  // Ethernet should be the first one regardless of strength, pressing it should
  // skip password and connection screens.
  screen_network_.SetIndexForTest(0);
  EXPECT_CALL(mock_screen_controller_, OnForward(testing::_));
  screen_network_.OnKeyPress(KEY_ENTER);
}

TEST_F(ScreenNetworkTest, GetNetworksRefresh) {
  screen_network_.SetStateForTest(NetworkState::kDropdownOpen);
  // Menu count is updated amd drop down screen is refreshed.
  screen_network_.OnGetNetworks(
      {{.ssid = "test1"}, {.ssid = "test2"}, {.ssid = "test3"}}, nullptr);
  // Update button when "refreshing" to the expanded dropdown screen.
  EXPECT_EQ(screen_network_.GetButtonCountForTest(), 4);
}

TEST_F(ScreenNetworkTest, EnterOnDropDown) {
  // If dropdown has not been selected yet, the focus is on the normal buttons.
  screen_network_.OnKeyPress(KEY_DOWN);
  EXPECT_CALL(mock_screen_controller_, OnBackward(testing::_));
  screen_network_.OnKeyPress(KEY_ENTER);

  // Set networks.
  screen_network_.OnGetNetworks(
      {{.ssid = "test1"}, {.ssid = "test2"}, {.ssid = "test3"}}, nullptr);

  // Select dropdown.
  screen_network_.OnKeyPress(KEY_UP);
  screen_network_.OnKeyPress(KEY_ENTER);
  screen_network_.SetStateForTest(NetworkState::kDropdownOpen);

  // Pick second network.
  screen_network_.OnKeyPress(KEY_DOWN);
  screen_network_.OnKeyPress(KEY_ENTER);

  EXPECT_EQ(screen_network_.GetIndexForTest(), 1);
}

TEST_F(ScreenNetworkTest, EscOnDropDown) {
  // Set networks.
  screen_network_.OnGetNetworks(
      {{.ssid = "test1"}, {.ssid = "test2"}, {.ssid = "test3"}}, nullptr);

  // Select dropdown.
  screen_network_.OnKeyPress(KEY_ENTER);
  screen_network_.SetStateForTest(NetworkState::kDropdownOpen);

  // Pick second network, then cancel selection by ESC.
  screen_network_.OnKeyPress(KEY_DOWN);
  screen_network_.OnKeyPress(KEY_ESC);

  EXPECT_EQ(screen_network_.GetStateForTest(), NetworkState::kDropdownClosed);
}

TEST_F(ScreenNetworkTest, NetworkNoPassword) {
  // Set networks.
  screen_network_.OnGetNetworks({{.ssid = "test1", .security = "none"}},
                                nullptr);

  screen_network_.SetStateForTest(NetworkState::kDropdownOpen);
  screen_network_.SetIndexForTest(0);

  // Pick first network
  screen_network_.OnKeyPress(KEY_ENTER);

  // Should skip password screen and wait for connection.
  EXPECT_EQ(screen_network_.GetStateForTest(),
            NetworkState::kWaitForConnection);
}

TEST_F(ScreenNetworkTest, OnConnectError) {
  std::string chosen_network = "test-ssid";
  // Network error, show corresponding screen.
  brillo::ErrorPtr error_ptr =
      brillo::Error::Create(FROM_HERE, "HTTP", "404", "Not found", nullptr);

  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kConnectionError));
  screen_network_.OnConnect(chosen_network, error_ptr.get());
}

TEST_F(ScreenNetworkTest, OnPasswordError) {
  std::string chosen_network = "test-ssid";
  // Network error, show corresponding screen.
  brillo::ErrorPtr error_ptr = brillo::Error::Create(
      FROM_HERE, "Password", "org.chromium.flimflam.Error.InvalidPassphrase",
      "Invalid passphrase", nullptr);

  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kPasswordError));
  screen_network_.OnConnect(chosen_network, error_ptr.get());
}

TEST_F(ScreenNetworkTest, GetNetworksRefreshError) {
  screen_network_.SetStateForTest(NetworkState::kDropdownOpen);
  // Network error.
  brillo::ErrorPtr error_ptr =
      brillo::Error::Create(FROM_HERE, "HTTP", "404", "Not found", nullptr);

  // Reset and show error screen.
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kNetworkError));
  screen_network_.OnGetNetworks({}, error_ptr.get());
  EXPECT_EQ(screen_network_.GetIndexForTest(), 1);
  EXPECT_EQ(screen_network_.GetButtonCountForTest(), 4);
  EXPECT_EQ(screen_network_.GetStateForTest(), NetworkState::kDropdownClosed);
}

}  // namespace minios
