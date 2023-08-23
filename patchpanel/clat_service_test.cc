// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/clat_service.h"

#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ip_address_utils.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

#include "patchpanel/shill_client.h"

using testing::_;
using testing::Exactly;
using testing::Invoke;

namespace patchpanel {
namespace {

MATCHER_P(ShillDeviceHasInterfaceName, expected_ifname, "") {
  return arg.ifname == expected_ifname;
}

class ClatServiceUnderTest : public ClatService {
 public:
  // TODO(b/278970851): Do the actual implementation. ClatService class needs to
  // take Datapath* argument in constructor.
  ClatServiceUnderTest() {
    ON_CALL(*this, StartClat(_))
        .WillByDefault(
            (Invoke(this, &ClatServiceUnderTest::SetClatRunningDeviceForTest)));
    ON_CALL(*this, StopClat())
        .WillByDefault(
            Invoke(this, &ClatServiceUnderTest::ResetClatRunningDeviceForTest));
  }

  MOCK_METHOD(void,
              StartClat,
              (const ShillClient::Device& shill_device),
              (override));

  MOCK_METHOD(void, StopClat, (), (override));
};

constexpr char kIPv4CIDR[] = "10.10.0.2/16";
constexpr char kIPv6CIDR[] = "2001:db8::1/64";

ShillClient::Device MakeFakeShillDevice(const std::string& ifname,
                                        int ifindex) {
  ShillClient::Device dev;
  dev.type = ShillClient::Device::Type::kEthernet;
  dev.ifindex = ifindex;
  dev.ifname = ifname;

  return dev;
}

ShillClient::Device MakeFakeIPv4OnlyShillDevice(
    const std::string& ifname,
    int ifindex = 1,
    const char ipv4_cidr[] = kIPv4CIDR) {
  ShillClient::Device dev = MakeFakeShillDevice(ifname, ifindex);
  dev.ipconfig.ipv4_cidr = net_base::IPv4CIDR::CreateFromCIDRString(ipv4_cidr);

  return dev;
}

ShillClient::Device MakeFakeIPv6OnlyShillDevice(
    const std::string& ifname,
    int ifindex = 1,
    const char ipv6_cidr[] = kIPv6CIDR) {
  ShillClient::Device dev = MakeFakeShillDevice(ifname, ifindex);
  dev.ipconfig.ipv6_cidr = net_base::IPv6CIDR::CreateFromCIDRString(ipv6_cidr);

  return dev;
}

ShillClient::Device MakeFakeDualStackShillDevice(
    const std::string& ifname,
    int ifindex = 1,
    const char ipv4_cidr[] = kIPv4CIDR,
    const char ipv6_cidr[] = kIPv6CIDR) {
  ShillClient::Device dev = MakeFakeShillDevice(ifname, ifindex);
  dev.ipconfig.ipv4_cidr = net_base::IPv4CIDR::CreateFromCIDRString(ipv4_cidr);
  dev.ipconfig.ipv6_cidr = net_base::IPv6CIDR::CreateFromCIDRString(ipv6_cidr);

  return dev;
}

}  // namespace

// TODO(b/278970851): Merge tests for OnShillDefaultLogicalDeviceChanged into a
// single test with a testcase data array.
TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromIPv4DeviceToIPv6OnlyDevice) {
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  ClatServiceUnderTest target;

  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, &v4only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromIPv6OnlyDeviceToIPv4Device) {
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  ClatServiceUnderTest target;
  // Start CLAT on the new_device.
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(target, StopClat());
  target.OnShillDefaultLogicalDeviceChanged(&v4only_dev, &v6only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromIPv6OnlyDeviceToAnother) {
  const auto new_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("new_v6only", 1, "1020:db8::1/64");
  const auto prev_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("prev_v6only", 1, "2001:db8::2/64");

  ClatServiceUnderTest target;
  // Start CLAT on the new_device.
  target.OnShillDefaultLogicalDeviceChanged(&prev_v6only_dev, nullptr);

  EXPECT_CALL(target, StopClat());
  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("new_v6only")));

  target.OnShillDefaultLogicalDeviceChanged(&new_v6only_dev, &prev_v6only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromDualStackDeviceToIPv4OnlyDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 2);

  ClatServiceUnderTest target;
  target.OnShillDefaultLogicalDeviceChanged(&dual_dev, nullptr);

  EXPECT_CALL(target, StopClat()).Times(Exactly(0));
  EXPECT_CALL(target, StartClat(_)).Times(Exactly(0));
  target.OnShillDefaultLogicalDeviceChanged(&v4only_dev, &dual_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromIPv4OnlyDeviceToDualStackDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 2);

  ClatServiceUnderTest target;
  target.OnShillDefaultLogicalDeviceChanged(&v4only_dev, nullptr);

  EXPECT_CALL(target, StopClat()).Times(Exactly(0));
  EXPECT_CALL(target, StartClat(_)).Times(Exactly(0));
  target.OnShillDefaultLogicalDeviceChanged(&dual_dev, &v4only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromDualStackDeviceToIPv6OnlyDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  ClatServiceUnderTest target;
  target.OnShillDefaultLogicalDeviceChanged(&dual_dev, nullptr);

  EXPECT_CALL(target, StopClat()).Times(Exactly(0));
  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, &dual_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromIPv6OnlyDeviceToDualStackDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  ClatServiceUnderTest target;
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(target, StopClat());
  EXPECT_CALL(target, StartClat(_)).Times(Exactly(0));
  target.OnShillDefaultLogicalDeviceChanged(&dual_dev, &v6only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromDualStackDeviceToAnother) {
  const auto new_v6only_dev = MakeFakeDualStackShillDevice(
      "new_dual_dev", 1, "10.10.0.2/24", "1020:db8::1/64");

  const auto prev_v6only_dev = MakeFakeDualStackShillDevice(
      "prev_dual_dev", 2, "10.20.0.2/24", "2001:db8::1/64");

  ClatServiceUnderTest target;
  target.OnShillDefaultLogicalDeviceChanged(&prev_v6only_dev, nullptr);

  EXPECT_CALL(target, StopClat()).Times(Exactly(0));
  EXPECT_CALL(target, StartClat(_)).Times(Exactly(0));
  target.OnShillDefaultLogicalDeviceChanged(&new_v6only_dev, &prev_v6only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromNonExstingDeviceToExistingDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only");

  ClatServiceUnderTest target;

  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     ChangeFromExstingDeviceToNonExistingDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only");

  ClatServiceUnderTest target;
  // Start CLAT on the new_device.
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(target, StopClat());
  EXPECT_CALL(target, StartClat(_)).Times(Exactly(0));
  target.OnShillDefaultLogicalDeviceChanged(nullptr, &v6only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     DefaultDeviceChangeWhileClatIsRunningOnDifferentDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 1);
  const auto prev_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("new_v6only", 2, "1020:db8::1/64");
  const auto new_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("prev_v6only", 3, "1030:db8::1/64");

  ClatServiceUnderTest target;
  // Start CLAT on device "v6only_dev1".
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  // Unexpectedly the default logical device changes from an device different
  // from v6only_dev1 to another.
  EXPECT_CALL(target, StopClat());
  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("new_v6only")));
  target.OnShillDefaultLogicalDeviceChanged(&prev_v6only_dev, &prev_v6only_dev);
}

TEST(ClatServiceDefaultLogicalDeviceChangeTest,
     NewDefaultDeviceIsTheSameWithClatDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 1);
  const auto dual_dev = MakeFakeDualStackShillDevice("dual", 2);

  ClatServiceUnderTest target;
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(target, StopClat()).Times(Exactly(0));
  EXPECT_CALL(target, StartClat(_)).Times(Exactly(0));
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, &dual_dev);
}

TEST(ClatServiceIPConfigChangeTest, IPv6OnlyDeviceGetIPv4Address) {
  auto default_logical_device = MakeFakeIPv6OnlyShillDevice("v6only");

  ClatServiceUnderTest target;

  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target.OnDefaultLogicalDeviceIPConfigChanged(default_logical_device);

  // The default logical device gets IPv4 address because of IPConfig changes.
  default_logical_device.ipconfig.ipv4_cidr =
      net_base::IPv4CIDR::CreateFromCIDRString(kIPv4CIDR);

  EXPECT_CALL(target, StopClat());
  target.OnDefaultLogicalDeviceIPConfigChanged(default_logical_device);
}

TEST(ClatServiceClatServiceIPConfigChangeTest, DeviceLoseIPv4Address) {
  auto default_logical_device = MakeFakeDualStackShillDevice("dual_stack", 1);

  ClatServiceUnderTest target;

  // The default logical device loses IPv4 address because of IPConfig changes.
  default_logical_device.ipconfig.ipv4_cidr.reset();

  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("dual_stack")));
  target.OnDefaultLogicalDeviceIPConfigChanged(default_logical_device);
}

TEST(ClatServiceIPConfigChangeTest, IPConfigChangeWithoutIPv6AddressChange) {
  auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only");
  v6only_dev.ipconfig.ipv4_dns_addresses = std::vector<std::string>{"8.8.8.8"};

  ClatServiceUnderTest target;
  target.OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);

  v6only_dev.ipconfig.ipv4_dns_addresses = std::vector<std::string>{"1.1.1.1"};

  // This change has nothing with CLAT.
  EXPECT_CALL(target, StopClat()).Times(Exactly(0));
  EXPECT_CALL(target, StartClat(_)).Times(Exactly(0));
  target.OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);
}

TEST(ClatServiceIPConfigChangeTest, IPv6AddressChangeInTheSamePrefix) {
  auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 1, "2001:db8::1/64");

  ClatServiceUnderTest target;
  target.OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);

  v6only_dev.ipconfig.ipv6_cidr =
      net_base::IPv6CIDR::CreateFromCIDRString("2001:db8::2/64");

  // Even the new IPn6 address of the default logical device has the same prefix
  // as the old one, CLAT needs to be reconfigured because the new address
  // conflict with the IPv6 address used by CLAT.
  EXPECT_CALL(target, StopClat());
  EXPECT_CALL(target, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target.OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);
}

}  // namespace patchpanel
