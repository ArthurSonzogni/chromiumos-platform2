// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_manager.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/network/mock_dhcp_client_proxy.h"
#include "shill/network/network.h"

namespace shill {
namespace {

constexpr int kInterfaceIndex = 3;
constexpr std::string_view kInterfaceName = "wlan1";
constexpr Technology kTechnology = Technology::kWiFi;
constexpr bool kFixedIPParams = false;

class NetworkManagerTest : public testing::Test {
 public:
  NetworkManagerTest() {
    EXPECT_CALL(mock_control_, CreateDHCPClientProxyFactory).WillOnce([]() {
      return std::make_unique<MockDHCPClientProxyFactory>();
    });
    network_manager_ =
        std::make_unique<NetworkManager>(&mock_control_, nullptr, nullptr);
  }

  MockControl mock_control_;
  std::unique_ptr<NetworkManager> network_manager_;
};

TEST_F(NetworkManagerTest, CreateNetwork) {
  // Creating a Network instance should success.
  std::unique_ptr<Network> network = network_manager_->CreateNetwork(
      kInterfaceIndex, std::string(kInterfaceName), kTechnology, kFixedIPParams,
      nullptr);
  ASSERT_NE(network, nullptr);

  // Querying a Network instance after creating should success.
  const int network_id = network->network_id();
  EXPECT_EQ(network_manager_->GetNetwork(network_id), network.get());

  // After the Network instance is destroyed, querying should fail.
  network.reset();
  EXPECT_EQ(network_manager_->GetNetwork(network_id), nullptr);
}

TEST_F(NetworkManagerTest, SetCapportEnabled) {
  std::unique_ptr<Network> network1 = network_manager_->CreateNetwork(
      kInterfaceIndex, std::string(kInterfaceName), kTechnology, kFixedIPParams,
      nullptr);

  // CAPPORT should be enabled for all the network instances.
  network_manager_->SetCapportEnabled(false);
  std::unique_ptr<Network> network2 = network_manager_->CreateNetwork(
      kInterfaceIndex, std::string(kInterfaceName), kTechnology, kFixedIPParams,
      nullptr);
  EXPECT_FALSE(network1->GetCapportEnabled());
  EXPECT_FALSE(network2->GetCapportEnabled());

  // CAPPORT should be disabled for all the network instances.
  network_manager_->SetCapportEnabled(true);
  std::unique_ptr<Network> network3 = network_manager_->CreateNetwork(
      kInterfaceIndex, std::string(kInterfaceName), kTechnology, kFixedIPParams,
      nullptr);
  EXPECT_TRUE(network1->GetCapportEnabled());
  EXPECT_TRUE(network2->GetCapportEnabled());
  EXPECT_TRUE(network3->GetCapportEnabled());
}

}  // namespace
}  // namespace shill
