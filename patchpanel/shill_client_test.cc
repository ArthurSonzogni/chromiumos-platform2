// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/shill_client.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_shill_client.h"

namespace patchpanel {
namespace {

class ShillClientTest : public testing::Test {
 protected:
  void SetUp() override {
    helper_ = std::make_unique<FakeShillClientHelper>();
    client_ = helper_->FakeClient();
    client_->RegisterDefaultLogicalDeviceChangedHandler(base::BindRepeating(
        &ShillClientTest::DefaultLogicalDeviceChangedHandler,
        base::Unretained(this)));
    client_->RegisterDefaultPhysicalDeviceChangedHandler(base::BindRepeating(
        &ShillClientTest::DefaultPhysicalDeviceChangedHandler,
        base::Unretained(this)));
    client_->RegisterDevicesChangedHandler(base::BindRepeating(
        &ShillClientTest::DevicesChangedHandler, base::Unretained(this)));
    client_->RegisterIPConfigsChangedHandler(base::BindRepeating(
        &ShillClientTest::IPConfigsChangedHandler, base::Unretained(this)));
    client_->RegisterIPv6NetworkChangedHandler(base::BindRepeating(
        &ShillClientTest::IPv6NetworkChangedHandler, base::Unretained(this)));
    default_logical_device_ = std::nullopt;
    default_physical_device_ = std::nullopt;
    added_.clear();
    removed_.clear();
  }

  void DefaultLogicalDeviceChangedHandler(
      const ShillClient::Device* new_device,
      const ShillClient::Device* prev_device) {
    if (new_device) {
      default_logical_device_ = *new_device;
    } else {
      default_logical_device_ = std::nullopt;
    }
  }

  void DefaultPhysicalDeviceChangedHandler(
      const ShillClient::Device* new_device,
      const ShillClient::Device* prev_device) {
    if (new_device) {
      default_physical_device_ = *new_device;
    } else {
      default_physical_device_ = std::nullopt;
    }
  }

  void DevicesChangedHandler(const std::vector<ShillClient::Device>& added,
                             const std::vector<ShillClient::Device>& removed) {
    added_ = added;
    removed_ = removed;
  }

  void IPConfigsChangedHandler(const ShillClient::Device& device) {
    ipconfig_change_calls_.push_back(device);
  }

  void IPv6NetworkChangedHandler(const ShillClient::Device& device) {
    ipv6_network_change_calls_.push_back(device);
  }

 protected:
  std::optional<ShillClient::Device> default_logical_device_;
  std::optional<ShillClient::Device> default_physical_device_;
  std::vector<ShillClient::Device> added_;
  std::vector<ShillClient::Device> removed_;
  std::vector<ShillClient::Device> ipconfig_change_calls_;
  std::vector<ShillClient::Device> ipv6_network_change_calls_;
  std::unique_ptr<FakeShillClient> client_;
  std::unique_ptr<FakeShillClientHelper> helper_;
};

TEST_F(ShillClientTest, DevicesChangedHandlerCalledOnDevicesPropertyChange) {
  dbus::ObjectPath eth0_path = dbus::ObjectPath("/device/eth0");
  ShillClient::Device eth0_dev;
  eth0_dev.technology = net_base::Technology::kEthernet;
  eth0_dev.ifindex = 1;
  eth0_dev.ifname = "eth0";
  eth0_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(eth0_path, eth0_dev);

  dbus::ObjectPath eth1_path = dbus::ObjectPath("/device/eth1");
  ShillClient::Device eth1_dev;
  eth1_dev.technology = net_base::Technology::kEthernet;
  eth1_dev.ifindex = 2;
  eth1_dev.ifname = "eth1";
  eth1_dev.service_path = "/service/2";
  client_->SetFakeDeviceProperties(eth1_path, eth1_dev);

  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.technology = net_base::Technology::kWiFi;
  wlan_dev.ifindex = 3;
  wlan_dev.ifname = "wlan0";
  wlan_dev.service_path = "/service/3";
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);

  std::vector<dbus::ObjectPath> devices = {eth0_path, wlan0_path};
  auto value = brillo::Any(devices);
  client_->SetFakeDefaultLogicalDevice("eth0");
  client_->SetFakeDefaultPhysicalDevice("eth0");

  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  EXPECT_EQ(added_.size(), devices.size());
  EXPECT_NE(std::find_if(added_.begin(), added_.end(),
                         [](const ShillClient::Device& dev) {
                           return dev.ifname == "eth0";
                         }),
            added_.end());
  EXPECT_NE(std::find_if(added_.begin(), added_.end(),
                         [](const ShillClient::Device& dev) {
                           return dev.ifname == "wlan0";
                         }),
            added_.end());
  EXPECT_EQ(removed_.size(), 0);

  // Implies the default callback was run;
  EXPECT_EQ(default_logical_device_->ifname, "eth0");
  EXPECT_EQ(default_physical_device_->ifname, "eth0");
  EXPECT_NE(std::find_if(added_.begin(), added_.end(),
                         [this](const ShillClient::Device& dev) {
                           return dev.ifname == default_logical_device_->ifname;
                         }),
            added_.end());

  devices.pop_back();
  devices.emplace_back(eth1_path);
  value = brillo::Any(devices);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  EXPECT_EQ(added_.size(), 1);
  EXPECT_EQ(added_[0].ifname, "eth1");
  EXPECT_EQ(removed_.size(), 1);
  EXPECT_EQ(removed_[0].ifname, "wlan0");
}

TEST_F(ShillClientTest, VerifyDevicesPrefixStripped) {
  dbus::ObjectPath eth0_path = dbus::ObjectPath("/device/eth0");
  ShillClient::Device eth0_dev;
  eth0_dev.technology = net_base::Technology::kEthernet;
  eth0_dev.ifindex = 1;
  eth0_dev.ifname = "eth0";
  eth0_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(eth0_path, eth0_dev);
  std::vector<dbus::ObjectPath> devices = {eth0_path};
  auto value = brillo::Any(devices);
  client_->SetFakeDefaultLogicalDevice("eth0");
  client_->SetFakeDefaultPhysicalDevice("eth0");

  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  EXPECT_EQ(added_.size(), 1);
  EXPECT_EQ(added_[0].ifname, "eth0");
  // Implies the default callback was run;
  EXPECT_EQ(default_logical_device_->ifname, "eth0");
  EXPECT_EQ(default_physical_device_->ifname, "eth0");
}

TEST_F(ShillClientTest, DefaultDeviceChangedHandlerCalledOnNewDefaultDevice) {
  client_->SetFakeDefaultLogicalDevice("eth0");
  client_->SetFakeDefaultPhysicalDevice("eth0");
  client_->NotifyManagerPropertyChange(shill::kDefaultServiceProperty,
                                       brillo::Any() /* ignored */);
  ASSERT_TRUE(default_logical_device_.has_value());
  ASSERT_TRUE(default_physical_device_.has_value());
  EXPECT_EQ(default_logical_device_->ifname, "eth0");
  EXPECT_EQ(default_physical_device_->ifname, "eth0");

  client_->SetFakeDefaultLogicalDevice("wlan0");
  client_->SetFakeDefaultPhysicalDevice("wlan0");
  client_->NotifyManagerPropertyChange(shill::kDefaultServiceProperty,
                                       brillo::Any() /* ignored */);
  ASSERT_TRUE(default_logical_device_.has_value());
  ASSERT_TRUE(default_physical_device_.has_value());
  EXPECT_EQ(default_logical_device_->ifname, "wlan0");
  EXPECT_EQ(default_physical_device_->ifname, "wlan0");
}

TEST_F(ShillClientTest, DefaultDeviceChangedHandlerNotCalledForSameDefault) {
  client_->SetFakeDefaultLogicalDevice("eth0");
  client_->SetFakeDefaultPhysicalDevice("eth0");
  client_->NotifyManagerPropertyChange(shill::kDefaultServiceProperty,
                                       brillo::Any() /* ignored */);
  ASSERT_TRUE(default_logical_device_.has_value());
  ASSERT_TRUE(default_physical_device_.has_value());
  EXPECT_EQ(default_logical_device_->ifname, "eth0");
  EXPECT_EQ(default_physical_device_->ifname, "eth0");

  default_logical_device_ = std::nullopt;
  default_physical_device_ = std::nullopt;
  client_->NotifyManagerPropertyChange(shill::kDefaultServiceProperty,
                                       brillo::Any() /* ignored */);
  // Implies the callback was not run the second time.
  EXPECT_FALSE(default_logical_device_.has_value());
  EXPECT_FALSE(default_physical_device_.has_value());
}

TEST_F(ShillClientTest, DefaultDeviceChanges) {
  dbus::ObjectPath eth0_path = dbus::ObjectPath("/device/eth0");
  ShillClient::Device eth0_dev;
  eth0_dev.technology = net_base::Technology::kEthernet;
  eth0_dev.ifindex = 1;
  eth0_dev.ifname = "eth0";
  eth0_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(eth0_path, eth0_dev);

  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.technology = net_base::Technology::kWiFi;
  wlan_dev.ifindex = 3;
  wlan_dev.ifname = "wlan0";
  wlan_dev.service_path = "/service/3";
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);

  // There is no network initially.
  ASSERT_EQ(nullptr, client_->default_logical_device());
  ASSERT_EQ(nullptr, client_->default_physical_device());

  // One network device appears.
  std::vector<dbus::ObjectPath> devices = {wlan0_path};
  auto value = brillo::Any(devices);
  client_->SetFakeDefaultLogicalDevice("wlan0");
  client_->SetFakeDefaultPhysicalDevice("wlan0");
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  ASSERT_TRUE(default_logical_device_.has_value());
  ASSERT_TRUE(default_physical_device_.has_value());
  EXPECT_EQ(default_logical_device_->ifname, "wlan0");
  EXPECT_EQ(default_physical_device_->ifname, "wlan0");
  ASSERT_NE(nullptr, client_->default_logical_device());
  ASSERT_NE(nullptr, client_->default_physical_device());
  EXPECT_EQ(client_->default_logical_device()->ifname, "wlan0");
  EXPECT_EQ(client_->default_physical_device()->ifname, "wlan0");

  // A second device appears, the default interface does not change yet.
  devices = {eth0_path, wlan0_path};
  value = brillo::Any(devices);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  ASSERT_TRUE(default_logical_device_.has_value());
  ASSERT_TRUE(default_physical_device_.has_value());
  EXPECT_EQ(default_logical_device_->ifname, "wlan0");
  EXPECT_EQ(default_physical_device_->ifname, "wlan0");
  ASSERT_NE(nullptr, client_->default_logical_device());
  ASSERT_NE(nullptr, client_->default_physical_device());
  EXPECT_EQ(client_->default_logical_device()->ifname, "wlan0");
  EXPECT_EQ(client_->default_physical_device()->ifname, "wlan0");

  // The second device becomes the default interface.
  client_->SetFakeDefaultLogicalDevice("eth0");
  client_->SetFakeDefaultPhysicalDevice("eth0");
  client_->NotifyManagerPropertyChange(shill::kDefaultServiceProperty,
                                       brillo::Any() /* ignored */);
  ASSERT_TRUE(default_logical_device_.has_value());
  ASSERT_TRUE(default_physical_device_.has_value());
  EXPECT_EQ(default_logical_device_->ifname, "eth0");
  EXPECT_EQ(default_physical_device_->ifname, "eth0");
  ASSERT_NE(nullptr, client_->default_logical_device());
  ASSERT_NE(nullptr, client_->default_physical_device());
  EXPECT_EQ(client_->default_logical_device()->ifname, "eth0");
  EXPECT_EQ(client_->default_physical_device()->ifname, "eth0");

  // The first device disappears.
  devices = {eth0_path};
  value = brillo::Any(devices);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  // The default device is still the same.
  EXPECT_EQ(default_logical_device_->ifname, "eth0");
  EXPECT_EQ(default_physical_device_->ifname, "eth0");
  EXPECT_EQ(client_->default_logical_device()->ifname, "eth0");
  EXPECT_EQ(client_->default_physical_device()->ifname, "eth0");

  // All devices have disappeared.
  devices = {};
  value = brillo::Any(devices);
  client_->SetFakeDefaultLogicalDevice(std::nullopt);
  client_->SetFakeDefaultPhysicalDevice(std::nullopt);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  ASSERT_FALSE(default_logical_device_.has_value());
  ASSERT_FALSE(default_physical_device_.has_value());
  ASSERT_EQ(nullptr, client_->default_logical_device());
  ASSERT_EQ(nullptr, client_->default_physical_device());
}

TEST_F(ShillClientTest, ListenToDeviceChangeSignalOnNewDevices) {
  dbus::ObjectPath eth0_path = dbus::ObjectPath("/device/eth0");
  ShillClient::Device eth0_dev;
  eth0_dev.technology = net_base::Technology::kEthernet;
  eth0_dev.ifindex = 1;
  eth0_dev.ifname = "eth0";
  eth0_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(eth0_path, eth0_dev);

  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.technology = net_base::Technology::kWiFi;
  wlan_dev.ifindex = 3;
  wlan_dev.ifname = "wlan0";
  wlan_dev.service_path = "/service/3";
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);

  // Adds a device.
  std::vector<dbus::ObjectPath> devices = {wlan0_path};
  auto value = brillo::Any(devices);
  EXPECT_CALL(*helper_->mock_proxy(),
              DoConnectToSignal(shill::kFlimflamDeviceInterface,
                                shill::kMonitorPropertyChanged, _, _))
      .Times(1);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);

  // Adds another device. DoConnectToSignal() called only for the new added one.
  devices = {wlan0_path, eth0_path};
  value = brillo::Any(devices);
  EXPECT_CALL(*helper_->mock_proxy(),
              DoConnectToSignal(shill::kFlimflamDeviceInterface,
                                shill::kMonitorPropertyChanged, _, _))
      .Times(1);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
}

TEST_F(ShillClientTest, TriggerOnIPConfigsChangeHandlerOnce) {
  const auto kIPv4CIDR1 =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.10.48/24");
  const auto kIPv4CIDR2 =
      *net_base::IPv4CIDR::CreateFromCIDRString("10.10.10.10/24");
  const auto kIPv4DNS1 = *net_base::IPAddress::CreateFromString("1.1.1.1");

  net_base::NetworkConfig network_config1;
  network_config1.ipv4_address = kIPv4CIDR1;
  network_config1.dns_servers.push_back(kIPv4DNS1);

  net_base::NetworkConfig network_config2;
  network_config2.ipv4_address = kIPv4CIDR2;

  constexpr int kInterfaceIndex = 1;

  // Adds a fake WiFi device.
  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.technology = net_base::Technology::kWiFi;
  wlan_dev.ifindex = kInterfaceIndex;
  wlan_dev.ifname = "wlan0";
  wlan_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  std::vector<dbus::ObjectPath> devices = {wlan0_path};
  auto devices_value = brillo::Any(devices);

  // The device will first appear before it acquires a new IP configuration.
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);

  // Update IP configuration.
  client_->UpdateNetworkConfigCache(kInterfaceIndex, network_config1);
  ASSERT_EQ(ipconfig_change_calls_.size(), 1u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_cidr, kIPv4CIDR1);
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_dns_addresses,
            std::vector<std::string>({kIPv4DNS1.ToString()}));

  // No callback should be triggered for the same update.
  client_->UpdateNetworkConfigCache(kInterfaceIndex, network_config1);
  ASSERT_EQ(ipconfig_change_calls_.size(), 1u);

  // Update the config, callback should be triggered.
  client_->UpdateNetworkConfigCache(kInterfaceIndex, network_config2);
  ASSERT_EQ(ipconfig_change_calls_.size(), 2u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_cidr, kIPv4CIDR2);
  EXPECT_TRUE(
      ipconfig_change_calls_.back().ipconfig.ipv4_dns_addresses.empty());

  // Removes the device. The device will first lose its IP configuration before
  // disappearing.
  client_->ClearNetworkConfigCache(kInterfaceIndex);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 3u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_cidr, std::nullopt);
  EXPECT_TRUE(
      ipconfig_change_calls_.back().ipconfig.ipv4_dns_addresses.empty());

  // Adds the device again. The device will first appear before it acquires a
  // new IP configuration.
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);
  client_->UpdateNetworkConfigCache(kInterfaceIndex, network_config1);
  ASSERT_EQ(ipconfig_change_calls_.size(), 4u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_cidr, kIPv4CIDR1);
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_dns_addresses,
            std::vector<std::string>({kIPv4DNS1.ToString()}));
}

TEST_F(ShillClientTest, TriggerOnIPv6NetworkChangedHandler) {
  const auto kIPv6CIDR = *net_base::IPv6CIDR::CreateFromCIDRString(
      "2001:db8::aabb:ccdd:1122:eeff/64");
  const auto kIPv6Gateway =
      *net_base::IPv6Address::CreateFromString("fe80::abcd:1234");
  const auto kIPv6DNS =
      *net_base::IPAddress::CreateFromString("2001:db8::1111");
  net_base::NetworkConfig network_config;
  network_config.ipv6_addresses.push_back(kIPv6CIDR);
  network_config.ipv6_gateway = kIPv6Gateway;
  network_config.dns_servers.push_back(kIPv6DNS);

  constexpr int kInterfaceIndex = 1;

  // Adds a fake WiFi device.
  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.technology = net_base::Technology::kWiFi;
  wlan_dev.ifindex = kInterfaceIndex;
  wlan_dev.ifname = "wlan0";
  wlan_dev.service_path = "/service/1";
  std::vector<dbus::ObjectPath> devices = {wlan0_path};
  auto devices_value = brillo::Any(devices);

  // The device will first appear before it acquires a new IP configuration. The
  // listeners are triggered.
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);
  client_->UpdateNetworkConfigCache(kInterfaceIndex, network_config);
  ASSERT_EQ(ipconfig_change_calls_.size(), 1u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_cidr, kIPv6CIDR);
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_gateway, kIPv6Gateway);
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_dns_addresses,
            std::vector<std::string>({kIPv6DNS.ToString()}));
  ASSERT_EQ(ipv6_network_change_calls_.size(), 1u);
  EXPECT_EQ(ipv6_network_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipv6_network_change_calls_.back().ipconfig.ipv6_cidr, kIPv6CIDR);

  // Removes the device. The device will first lose its IP configuration before
  // disappearing.
  client_->ClearNetworkConfigCache(kInterfaceIndex);
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 2u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_cidr, std::nullopt);
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_gateway, std::nullopt);
  EXPECT_TRUE(
      ipconfig_change_calls_.back().ipconfig.ipv6_dns_addresses.empty());
  ASSERT_EQ(ipv6_network_change_calls_.size(), 2u);
  EXPECT_EQ(ipv6_network_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipv6_network_change_calls_.back().ipconfig.ipv6_cidr, std::nullopt);

  // Adds the device again. The device will first appear before it acquires a
  // new IP configuration, without DNS.
  network_config.dns_servers = {};
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  client_->UpdateNetworkConfigCache(kInterfaceIndex, network_config);
  ASSERT_EQ(ipconfig_change_calls_.size(), 3u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_cidr, kIPv6CIDR);
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_gateway, kIPv6Gateway);
  EXPECT_TRUE(
      ipconfig_change_calls_.back().ipconfig.ipv6_dns_addresses.empty());
  ASSERT_EQ(ipv6_network_change_calls_.size(), 3u);
  EXPECT_EQ(ipv6_network_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipv6_network_change_calls_.back().ipconfig.ipv6_cidr, kIPv6CIDR);

  // Adds IPv6 DNS, IPv6NetworkChangedHandler is not triggered.
  network_config.dns_servers.push_back(kIPv6DNS);
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  client_->UpdateNetworkConfigCache(kInterfaceIndex, network_config);
  ASSERT_EQ(ipconfig_change_calls_.size(), 4u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_dns_addresses,
            std::vector<std::string>({kIPv6DNS.ToString()}));
  ASSERT_EQ(ipv6_network_change_calls_.size(), 3u);
}

TEST_F(ShillClientTest, DoHProviderHandler) {
  // Helper function to trigger a chance event for the DNSProxyDOHProviders
  // property.
  const auto trigger_doh_change =
      [&](const std::map<std::string, std::string>& kvs) {
        brillo::VariantDictionary doh_val;
        for (const auto& [k, v] : kvs) {
          doh_val[k] = brillo::Any(v);
        }
        client_->NotifyManagerPropertyChange(
            shill::kDNSProxyDOHProvidersProperty, brillo::Any(doh_val));
      };

  // Helper class to register the callback.
  class FakeHandler {
   public:
    MOCK_METHOD(void,
                OnDoHProvidersChanged,
                (const ShillClient::DoHProviders&));
  } handler;

  // Constants used in this test.
  const std::string doh1 = "doh.server.1";
  const std::string doh2 = "doh.server.2";
  const std::string ips1 = "ip1,ip2,ip3";
  const std::string ips2 = "ip4,ip5,ip6";

  // Before any callback is registered, ShillClient knows |doh1|.
  trigger_doh_change({{doh1, ips1}});

  // Register the callback should trigger it immediately.
  EXPECT_CALL(handler, OnDoHProvidersChanged(ShillClient::DoHProviders{doh1}));
  client_->RegisterDoHProvidersChangedHandler(base::BindRepeating(
      &FakeHandler::OnDoHProvidersChanged, base::Unretained(&handler)));

  // The value change (i.e., the IP address list) won't trigger the callback.
  trigger_doh_change({{doh1, ips2}});

  // New DoH provider.
  EXPECT_CALL(handler,
              OnDoHProvidersChanged(ShillClient::DoHProviders{doh1, doh2}));
  trigger_doh_change({{doh1, ips1}, {doh2, ips2}});

  // Removal of an existing DoH provider.
  EXPECT_CALL(handler, OnDoHProvidersChanged(ShillClient::DoHProviders{doh2}));
  trigger_doh_change({{doh2, ips2}});
}

}  // namespace
}  // namespace patchpanel
