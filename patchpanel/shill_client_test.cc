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

#include <chromeos/dbus/service_constants.h>
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
  eth0_dev.type = ShillClient::Device::Type::kEthernet;
  eth0_dev.ifindex = 1;
  eth0_dev.ifname = "eth0";
  eth0_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(eth0_path, eth0_dev);

  dbus::ObjectPath eth1_path = dbus::ObjectPath("/device/eth1");
  ShillClient::Device eth1_dev;
  eth1_dev.type = ShillClient::Device::Type::kEthernet;
  eth1_dev.ifindex = 2;
  eth1_dev.ifname = "eth1";
  eth1_dev.service_path = "/service/2";
  client_->SetFakeDeviceProperties(eth1_path, eth1_dev);

  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.type = ShillClient::Device::Type::kWifi;
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
  eth0_dev.type = ShillClient::Device::Type::kEthernet;
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
  eth0_dev.type = ShillClient::Device::Type::kEthernet;
  eth0_dev.ifindex = 1;
  eth0_dev.ifname = "eth0";
  eth0_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(eth0_path, eth0_dev);

  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.type = ShillClient::Device::Type::kWifi;
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
  eth0_dev.type = ShillClient::Device::Type::kEthernet;
  eth0_dev.ifindex = 1;
  eth0_dev.ifname = "eth0";
  eth0_dev.service_path = "/service/1";
  client_->SetFakeDeviceProperties(eth0_path, eth0_dev);

  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.type = ShillClient::Device::Type::kWifi;
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
  // Adds a fake WiFi device.
  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.type = ShillClient::Device::Type::kWifi;
  wlan_dev.ifindex = 1;
  wlan_dev.ifname = "wlan0";
  wlan_dev.service_path = "/service/1";
  wlan_dev.ipconfig.ipv4_cidr =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.10.48/24");
  wlan_dev.ipconfig.ipv4_gateway = net_base::IPv4Address(192, 168, 10, 1);
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  std::vector<dbus::ObjectPath> devices = {wlan0_path};
  auto devices_value = brillo::Any(devices);

  // The device will first appear before it acquires a new IP configuration.
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);

  // Spurious shill::kIPConfigsProperty update with no configuration change,
  // listeners are not triggered.
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
  ASSERT_TRUE(ipconfig_change_calls_.empty());

  // Update IP configuration
  wlan_dev.ipconfig.ipv4_dns_addresses = {"1.1.1.1"};
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);

  // A shill::kIPConfigsProperty update triggers listeners.
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 1u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_cidr,
            *net_base::IPv4CIDR::CreateFromCIDRString("192.168.10.48/24"));
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_gateway,
            net_base::IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_dns_addresses,
            std::vector<std::string>({"1.1.1.1"}));

  // Removes the device. The device will first lose its IP configuration before
  // disappearing.
  ShillClient::Device disconnected_dev = wlan_dev;
  disconnected_dev.ipconfig = {};
  client_->SetFakeDeviceProperties(wlan0_path, disconnected_dev);
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 2u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_cidr, std::nullopt);
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_gateway, std::nullopt);
  EXPECT_TRUE(
      ipconfig_change_calls_.back().ipconfig.ipv4_dns_addresses.empty());

  // Adds the device again. The device will first appear before it acquires a
  // new IP configuration.
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 3u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_cidr,
            *net_base::IPv4CIDR::CreateFromCIDRString("192.168.10.48/24"));
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_gateway,
            net_base::IPv4Address(192, 168, 10, 1));
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv4_dns_addresses,
            std::vector<std::string>({"1.1.1.1"}));
}

TEST_F(ShillClientTest, TriggerOnIPv6NetworkChangedHandler) {
  // Adds a fake WiFi device.
  dbus::ObjectPath wlan0_path = dbus::ObjectPath("/device/wlan0");
  ShillClient::Device wlan_dev;
  wlan_dev.type = ShillClient::Device::Type::kWifi;
  wlan_dev.ifindex = 1;
  wlan_dev.ifname = "wlan0";
  wlan_dev.service_path = "/service/1";
  wlan_dev.ipconfig.ipv6_cidr = *net_base::IPv6CIDR::CreateFromCIDRString(
      "2001:db8::aabb:ccdd:1122:eeff/64");
  wlan_dev.ipconfig.ipv6_gateway =
      *net_base::IPv6Address::CreateFromString("fe80::abcd:1234");
  wlan_dev.ipconfig.ipv6_dns_addresses = {"2001:db8::1111"};
  std::vector<dbus::ObjectPath> devices = {wlan0_path};
  auto devices_value = brillo::Any(devices);

  // The device will first appear before it acquires a new IP configuration. The
  // listeners are triggered
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 1u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_cidr,
            *net_base::IPv6CIDR::CreateFromCIDRString(
                "2001:db8::aabb:ccdd:1122:eeff/64"));
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_gateway,
            *net_base::IPv6Address::CreateFromString("fe80::abcd:1234"));
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_dns_addresses,
            std::vector<std::string>({"2001:db8::1111"}));
  ASSERT_EQ(ipv6_network_change_calls_.size(), 1u);
  EXPECT_EQ(ipv6_network_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipv6_network_change_calls_.back().ipconfig.ipv6_cidr,
            *net_base::IPv6CIDR::CreateFromCIDRString(
                "2001:db8::aabb:ccdd:1122:eeff/64"));

  // Removes the device. The device will first lose its IP configuration before
  // disappearing.
  ShillClient::Device disconnected_dev = wlan_dev;
  disconnected_dev.ipconfig = {};
  client_->SetFakeDeviceProperties(wlan0_path, disconnected_dev);
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
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
  wlan_dev.ipconfig.ipv6_dns_addresses = {};
  client_->NotifyManagerPropertyChange(shill::kDevicesProperty, devices_value);
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 3u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_cidr,
            *net_base::IPv6CIDR::CreateFromCIDRString(
                "2001:db8::aabb:ccdd:1122:eeff/64"));
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_gateway,
            *net_base::IPv6Address::CreateFromString("fe80::abcd:1234"));
  EXPECT_TRUE(
      ipconfig_change_calls_.back().ipconfig.ipv6_dns_addresses.empty());
  ASSERT_EQ(ipv6_network_change_calls_.size(), 3u);
  EXPECT_EQ(ipv6_network_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipv6_network_change_calls_.back().ipconfig.ipv6_cidr,
            net_base::IPv6CIDR::CreateFromCIDRString(
                "2001:db8::aabb:ccdd:1122:eeff/64"));

  // Adds IPv6 DNS, IPv6NetworkChangedHandler is not triggered.
  wlan_dev.ipconfig.ipv6_dns_addresses = {"2001:db8::1111"};
  client_->SetFakeDeviceProperties(wlan0_path, wlan_dev);
  client_->NotifyDevicePropertyChange(wlan0_path, shill::kIPConfigsProperty,
                                      brillo::Any());
  ASSERT_EQ(ipconfig_change_calls_.size(), 4u);
  EXPECT_EQ(ipconfig_change_calls_.back().ifname, "wlan0");
  EXPECT_EQ(ipconfig_change_calls_.back().ipconfig.ipv6_dns_addresses,
            std::vector<std::string>({"2001:db8::1111"}));
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
