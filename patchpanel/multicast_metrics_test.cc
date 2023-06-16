// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_metrics.h"

#include <map>
#include <memory>
#include <utility>

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ipv4_address.h>

#include "metrics/metrics_library_mock.h"
#include "patchpanel/metrics.h"
#include "patchpanel/mock_multicast_counters_service.h"
#include "patchpanel/multicast_counters_service.h"
#include "patchpanel/shill_client.h"

using net_base::IPv4CIDR;
using ::testing::_;
using testing::Return;

namespace patchpanel {
namespace {

class MulticastMetricsTest : public testing::Test {
 protected:
  void SetUp() override {
    mock_metrics_lib_ = std::make_unique<MetricsLibraryMock>();
    multicast_metrics_ = std::make_unique<MulticastMetrics>(
        &counters_service_, mock_metrics_lib_.get());

    EXPECT_CALL(counters_service_, GetCounters())
        .WillRepeatedly(
            Return(std::map<MulticastCountersService::CounterKey, uint64_t>{}));
  }

  MockMulticastCountersService counters_service_;
  std::unique_ptr<MetricsLibraryMock> mock_metrics_lib_;
  std::unique_ptr<MulticastMetrics> multicast_metrics_;
  base::test::SingleThreadTaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
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

TEST_F(MulticastMetricsTest, ARC_StartStop) {
  ShillClient::Device device;
  device.ifname = "wlan0";
  device.type = ShillClient::Device::Type::kWifi;
  device.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  // WiFi device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device);
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // ARC started.
  multicast_metrics_->OnARCStarted();
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // WiFi device stopped.
  multicast_metrics_->OnPhysicalDeviceRemoved(device);
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // WiFi device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // ARC stopped.
  multicast_metrics_->OnARCStopped();
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());
}

TEST_F(MulticastMetricsTest, ARC_ForwardingStateChanges) {
  // Base ARC state.
  EXPECT_FALSE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // ARC multicast forwarders started.
  multicast_metrics_->OnARCWiFiForwarderStarted();
  EXPECT_TRUE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // ARC multicast forwarders stopped.
  multicast_metrics_->OnARCWiFiForwarderStopped();
  EXPECT_FALSE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
  EXPECT_FALSE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
}

TEST_F(MulticastMetricsTest, ARC_StartStopWithForwardingChanges) {
  ShillClient::Device device;
  device.ifname = "wlan0";
  device.type = ShillClient::Device::Type::kWifi;
  device.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  // ARC started.
  multicast_metrics_->OnARCStarted();
  EXPECT_FALSE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // ARC WiFi device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device);
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());
  EXPECT_FALSE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());

  // ARC multicast forwarders started.
  multicast_metrics_->OnARCWiFiForwarderStarted();
  EXPECT_TRUE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // ARC multicast forwarders stopped.
  multicast_metrics_->OnARCWiFiForwarderStopped();
  EXPECT_FALSE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  // ARC WiFi device stopped.
  multicast_metrics_->OnPhysicalDeviceRemoved(device);
  EXPECT_FALSE(
      multicast_metrics_->pollers_[Type::kARC]->IsARCForwardingEnabled());
  EXPECT_FALSE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());
}

// TODO(jasongustaman): Add multicast counters unit test alongside emitting the
// metrics.

TEST_F(MulticastMetricsTest, ARC_SendActiveTimeMetrics) {
  // Test active time metrics will be sent if ARC poller is started and
  // stopped.
  EXPECT_CALL(*mock_metrics_lib_,
              SendPercentageToUMA(kMulticastActiveTimeMetrics, 50))
      .Times(1);
  ShillClient::Device device;
  device.ifname = "wlan0";
  device.type = ShillClient::Device::Type::kWifi;
  device.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  // WiFi device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device);

  // ARC started.
  multicast_metrics_->OnARCStarted();
  task_environment.FastForwardBy(base::Minutes(2));
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());

  multicast_metrics_->OnARCWiFiForwarderStarted();
  task_environment.FastForwardBy(base::Minutes(2));

  // WiFi device stopped.
  multicast_metrics_->OnPhysicalDeviceRemoved(device);
}

TEST_F(MulticastMetricsTest, ARC_NotSendActiveTimeMetricsNoStop) {
  // Test active time metrics will not be sent if ARC poller is not
  // stopped.
  EXPECT_CALL(*mock_metrics_lib_,
              SendPercentageToUMA(kMulticastActiveTimeMetrics, _))
      .Times(0);
  ShillClient::Device device;
  device.ifname = "wlan0";
  device.type = ShillClient::Device::Type::kWifi;
  device.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  // WiFi device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device);

  // ARC started.
  multicast_metrics_->OnARCStarted();
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());
  task_environment.FastForwardBy(base::Minutes(2));

  multicast_metrics_->OnARCWiFiForwarderStarted();
  multicast_metrics_->OnARCWiFiForwarderStopped();
}

TEST_F(MulticastMetricsTest, ARC_NotSendActiveTimeMetricsARCNotRunning) {
  // Test active time metrics will not be sent if ARC is not running.
  EXPECT_CALL(*mock_metrics_lib_,
              SendPercentageToUMA(kMulticastActiveTimeMetrics, _))
      .Times(0);
  ShillClient::Device device;
  device.ifname = "wlan0";
  device.type = ShillClient::Device::Type::kWifi;
  device.ipconfig.ipv4_cidr = *IPv4CIDR::CreateFromCIDRString("1.2.3.4/32");

  // WiFi device added.
  multicast_metrics_->OnPhysicalDeviceAdded(device);

  // ARC started.
  multicast_metrics_->OnARCStarted();
  EXPECT_TRUE(multicast_metrics_->pollers_[Type::kARC]->IsTimerRunning());
  task_environment.FastForwardBy(base::Minutes(2));

  // ARC stopped.
  multicast_metrics_->OnARCStopped();

  // WiFi device removed.
  multicast_metrics_->OnPhysicalDeviceRemoved(device);
}

}  // namespace patchpanel
