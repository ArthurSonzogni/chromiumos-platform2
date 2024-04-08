// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_manager.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "shill/network/network.h"

namespace shill {
namespace {

constexpr int kInterfaceIndex = 3;
constexpr std::string_view kInterfaceName = "wlan1";
constexpr Technology kTechnology = Technology::kWiFi;
constexpr bool kFixedIPParams = false;

TEST(NetworkManagerTest, CreateNetwork) {
  NetworkManager network_manager(nullptr, nullptr, nullptr);
  // Creating a Network instance should success.
  std::unique_ptr<Network> network = network_manager.CreateNetwork(
      kInterfaceIndex, std::string(kInterfaceName), kTechnology, kFixedIPParams,
      nullptr);
  ASSERT_NE(network, nullptr);

  // Querying a Network instance after creating should success.
  const int network_id = network->network_id();
  EXPECT_EQ(network_manager.GetNetwork(network_id), network.get());

  // After the Network instance is destroyed, querying should fail.
  network.reset();
  EXPECT_EQ(network_manager.GetNetwork(network_id), nullptr);
}

}  // namespace
}  // namespace shill
