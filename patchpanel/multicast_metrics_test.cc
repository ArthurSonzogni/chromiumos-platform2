// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_metrics.h"

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ipv4_address.h>

#include <map>
#include <memory>
#include <utility>

#include "patchpanel/shill_client.h"

using net_base::IPv4CIDR;

namespace patchpanel {
namespace {

class MulticastMetricsTest : public testing::Test {
 protected:
  void SetUp() override {
    multicast_metrics_ = std::make_unique<MulticastMetrics>();
  }

  std::unique_ptr<MulticastMetrics> multicast_metrics_;
  base::test::SingleThreadTaskEnvironment task_environment;
};

}  // namespace

using Type = MulticastMetrics::Type;

TEST_F(MulticastMetricsTest, BaseState) {
  EXPECT_EQ(multicast_metrics_->pollers_.size(), 4);
  for (const auto& poller : multicast_metrics_->pollers_) {
    EXPECT_EQ(poller.second->ifnames().size(), 0);
    EXPECT_FALSE(poller.second->IsTimerRunning());
  }
}

TEST_F(MulticastMetricsTest, Total_StartStop) {
  multicast_metrics_->Start(Type::kTotal);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kTotal]->IsTimerRunning());
  multicast_metrics_->Stop(Type::kTotal);
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kTotal]->IsTimerRunning());
}

TEST_F(MulticastMetricsTest, NetworkTechnology_StartStop) {
  std::map<Type, std::string> technologies = {{Type::kEthernet, "eth0"},
                                              {Type::kWiFi, "wlan0"}};
  for (auto technology : technologies) {
    multicast_metrics_->Start(technology.first, technology.second);
    EXPECT_TRUE(
        multicast_metrics_->pollers_[technology.first]->IsTimerRunning());
  }
  for (auto technology : technologies) {
    multicast_metrics_->Stop(technology.first, technology.second);
    EXPECT_FALSE(
        multicast_metrics_->pollers_[technology.first]->IsTimerRunning());
  }
}

TEST_F(MulticastMetricsTest, IPConfigChanges_StartStop) {
  ShillClient::Device device;
  device.ifname = "eth0";
  device.type = ShillClient::Device::Type::kEthernet;
  device.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  // Device is connected.
  multicast_metrics_->OnIPConfigsChanged(device);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());

  // Other IPConfig changes.
  multicast_metrics_->OnIPConfigsChanged(device);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());

  // Device is disconnected.
  device.ipconfig.ipv4_cidr.reset();
  multicast_metrics_->OnIPConfigsChanged(device);
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());
}

TEST_F(MulticastMetricsTest, DeviceChanges_StartStop) {
  ShillClient::Device device;
  device.ifname = "eth0";
  device.type = ShillClient::Device::Type::kEthernet;

  // Device is added but not connected.
  multicast_metrics_->OnPhysicalDeviceAdded(device);
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());

  // Device is added and connected.
  device.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");
  multicast_metrics_->OnPhysicalDeviceAdded(device);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());

  // Device is removed.
  multicast_metrics_->OnPhysicalDeviceRemoved(device);
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());
}

TEST_F(MulticastMetricsTest, MultipleDeviceChanges_StartStop) {
  ShillClient::Device device0;
  device0.ifname = "eth0";
  device0.type = ShillClient::Device::Type::kEthernet;
  device0.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  ShillClient::Device device1;
  device1.ifname = "eth1";
  device1.type = ShillClient::Device::Type::kEthernet;
  device1.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  // First device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device0);
  EXPECT_EQ(multicast_metrics_->pollers_[Type::kEthernet]->ifnames().size(), 1);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());

  // Second device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device1);
  EXPECT_EQ(multicast_metrics_->pollers_[Type::kEthernet]->ifnames().size(), 2);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());

  // First device removed.
  multicast_metrics_->OnPhysicalDeviceRemoved(device0);
  EXPECT_EQ(multicast_metrics_->pollers_[Type::kEthernet]->ifnames().size(), 1);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());

  // Second device removed.
  multicast_metrics_->OnPhysicalDeviceRemoved(device1);
  EXPECT_EQ(multicast_metrics_->pollers_[Type::kEthernet]->ifnames().size(), 0);
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kEthernet]->IsTimerRunning());
}

}  // namespace patchpanel
