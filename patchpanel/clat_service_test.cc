// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/clat_service.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ip_address_utils.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/mock_process_manager.h>
#include <net-base/process_manager.h>

#include "patchpanel/datapath.h"
#include "patchpanel/fake_system.h"
#include "patchpanel/iptables.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/system.h"

using testing::_;
using testing::Eq;
using testing::Exactly;
using testing::Invoke;
using testing::IsEmpty;
using testing::Return;
using testing::StrEq;

namespace patchpanel {
namespace {

MATCHER_P(ShillDeviceHasInterfaceName, expected_ifname, "") {
  return arg.ifname == expected_ifname;
}

MATCHER_P(CIDRHasPrefix, expected_prefix_str, "") {
  return net_base::IPv6CIDR::CreateFromCIDRString(expected_prefix_str)
      ->InSameSubnetWith(arg.address());
}

MATCHER_P(AddressHasPrefix, expected_prefix_str, "") {
  return net_base::IPv6CIDR::CreateFromCIDRString(expected_prefix_str)
      ->InSameSubnetWith(arg);
}

class ClatServiceUnderTest : public ClatService {
 public:
  ClatServiceUnderTest(Datapath* datapath,
                       net_base::ProcessManager* process_manager,
                       System* system)
      : ClatService(datapath, process_manager, system) {
    ON_CALL(*this, StartClat(_))
        .WillByDefault(
            (Invoke(this, &ClatServiceUnderTest::SetClatRunningDeviceForTest)));
    ON_CALL(*this, StopClat(true))
        .WillByDefault(
            Invoke(this, &ClatServiceUnderTest::ResetClatRunningDeviceForTest));
    Enable();
  }

  MOCK_METHOD(void,
              StartClat,
              (const ShillClient::Device& shill_device),
              (override));

  MOCK_METHOD(void, StopClat, (bool clear_running_device), (override));
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
}  // namespace

class ClatServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    target_ = std::make_unique<ClatServiceUnderTest>(
        &datapath_, &process_manager_, &system_);
  }
  MockDatapath datapath_ = MockDatapath();
  net_base::MockProcessManager process_manager_ =
      net_base::MockProcessManager();
  FakeSystem system_ = FakeSystem();
  std::unique_ptr<ClatServiceUnderTest> target_;
};

// TODO(b/278970851): Merge tests for OnShillDefaultLogicalDeviceChanged into a
// single test with a testcase data array.
TEST_F(ClatServiceTest, ChangeFromIPv4DeviceToIPv6OnlyDevice) {
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, &v4only_dev);
}

TEST_F(ClatServiceTest, ChangeFromIPv6OnlyDeviceToIPv4Device) {
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  //  Start CLAT on the new_device.
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(true));
  target_->OnShillDefaultLogicalDeviceChanged(&v4only_dev, &v6only_dev);
}

TEST_F(ClatServiceTest, ChangeFromIPv6OnlyDeviceToAnother) {
  const auto new_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("new_v6only", 1, "1020:db8::1/64");
  const auto prev_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("prev_v6only", 1, "2001:db8::2/64");

  //  Start CLAT on the new_device.
  target_->OnShillDefaultLogicalDeviceChanged(&prev_v6only_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(true));
  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("new_v6only")));

  target_->OnShillDefaultLogicalDeviceChanged(&new_v6only_dev,
                                              &prev_v6only_dev);
}

TEST_F(ClatServiceTest, ChangeFromDualStackDeviceToIPv4OnlyDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 2);

  target_->OnShillDefaultLogicalDeviceChanged(&dual_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(_)).Times(Exactly(0));
  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->OnShillDefaultLogicalDeviceChanged(&v4only_dev, &dual_dev);
}

TEST_F(ClatServiceTest, ChangeFromIPv4OnlyDeviceToDualStackDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 2);

  target_->OnShillDefaultLogicalDeviceChanged(&v4only_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(_)).Times(Exactly(0));
  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->OnShillDefaultLogicalDeviceChanged(&dual_dev, &v4only_dev);
}

TEST_F(ClatServiceTest, ChangeFromDualStackDeviceToIPv6OnlyDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  target_->OnShillDefaultLogicalDeviceChanged(&dual_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(_)).Times(Exactly(0));
  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, &dual_dev);
}

TEST_F(ClatServiceTest, ChangeFromIPv6OnlyDeviceToDualStackDevice) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual_dev", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(true));
  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->OnShillDefaultLogicalDeviceChanged(&dual_dev, &v6only_dev);
}

TEST_F(ClatServiceTest, ChangeFromDualStackDeviceToAnother) {
  const auto new_v6only_dev = MakeFakeDualStackShillDevice(
      "new_dual_dev", 1, "10.10.0.2/24", "1020:db8::1/64");

  const auto prev_v6only_dev = MakeFakeDualStackShillDevice(
      "prev_dual_dev", 2, "10.20.0.2/24", "2001:db8::1/64");

  target_->OnShillDefaultLogicalDeviceChanged(&prev_v6only_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(_)).Times(Exactly(0));
  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->OnShillDefaultLogicalDeviceChanged(&new_v6only_dev,
                                              &prev_v6only_dev);
}

TEST_F(ClatServiceTest, ChangeFromNonExstingDeviceToExistingDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only");

  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);
}

TEST_F(ClatServiceTest, ChangeFromExstingDeviceToNonExistingDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only");

  //  Start CLAT on the new_device.
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(true));
  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->OnShillDefaultLogicalDeviceChanged(nullptr, &v6only_dev);
}

TEST_F(ClatServiceTest,
       DefaultDeviceChangeWhileClatIsRunningOnDifferentDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 1);
  const auto prev_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("new_v6only", 2, "1020:db8::1/64");
  const auto new_v6only_dev =
      MakeFakeIPv6OnlyShillDevice("prev_v6only", 3, "1030:db8::1/64");

  //  Start CLAT on device "v6only_dev1".
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  // Unexpectedly the default logical device changes from an device different
  // from v6only_dev1 to another.
  EXPECT_CALL(*target_, StopClat(true));
  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("new_v6only")));
  target_->OnShillDefaultLogicalDeviceChanged(&prev_v6only_dev,
                                              &prev_v6only_dev);
}

TEST_F(ClatServiceTest, NewDefaultDeviceIsTheSameWithClatDevice) {
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 1);
  const auto dual_dev = MakeFakeDualStackShillDevice("dual", 2);

  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(_)).Times(Exactly(0));
  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, &dual_dev);
}

TEST_F(ClatServiceTest,
       ChangeFromDualStackDeviceToIPv6OnlyDeviceWhileDisabled) {
  const auto dual_dev = MakeFakeDualStackShillDevice("dual", 1);
  const auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  target_->OnShillDefaultLogicalDeviceChanged(&dual_dev, nullptr);

  EXPECT_CALL(*target_, StopClat(false));
  target_->Disable();

  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target_->OnShillDefaultLogicalDeviceChanged(&v6only_dev, &dual_dev);

  // The default logical device is IPv6-only, so CLAT starts immdiately after
  // it's enabled.
  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target_->Enable();
}

TEST_F(ClatServiceTest, IPv6OnlyDeviceGetIPv4Address) {
  auto default_logical_device = MakeFakeIPv6OnlyShillDevice("v6only");

  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target_->OnDefaultLogicalDeviceIPConfigChanged(default_logical_device);

  // The default logical device gets IPv4 address because of IPConfig changes.
  default_logical_device.ipconfig.ipv4_cidr =
      net_base::IPv4CIDR::CreateFromCIDRString(kIPv4CIDR);

  EXPECT_CALL(*target_, StopClat(true));
  target_->OnDefaultLogicalDeviceIPConfigChanged(default_logical_device);
}

TEST_F(ClatServiceTest, DeviceLoseIPv4Address) {
  auto default_logical_device = MakeFakeDualStackShillDevice("dual_stack", 1);

  // The default logical device loses IPv4 address because of IPConfig changes.
  default_logical_device.ipconfig.ipv4_cidr.reset();

  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("dual_stack")));
  target_->OnDefaultLogicalDeviceIPConfigChanged(default_logical_device);
}

TEST_F(ClatServiceTest, IPConfigChangeWithoutIPv6AddressChange) {
  auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only");
  v6only_dev.ipconfig.ipv4_dns_addresses = std::vector<std::string>{"8.8.8.8"};

  target_->OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);

  v6only_dev.ipconfig.ipv4_dns_addresses = std::vector<std::string>{"1.1.1.1"};

  // This change has nothing with CLAT.
  EXPECT_CALL(*target_, StopClat(_)).Times(Exactly(0));
  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);
}

TEST_F(ClatServiceTest, IPv6AddressChangeInTheSamePrefix) {
  auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 1, "2001:db8::1/64");

  target_->OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);

  v6only_dev.ipconfig.ipv6_cidr =
      net_base::IPv6CIDR::CreateFromCIDRString("2001:db8::2/64");

  // Even the new IPn6 address of the default logical device has the same prefix
  // as the old one, CLAT needs to be reconfigured because the new address
  // conflict with the IPv6 address used by CLAT.
  EXPECT_CALL(*target_, StopClat(true));
  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("v6only")));
  target_->OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);
}

TEST_F(ClatServiceTest, EnabledAfterGettingIPv4AddressWhileDisabled) {
  auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only");

  target_->OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);

  EXPECT_CALL(*target_, StopClat(false));
  target_->Disable();

  v6only_dev.ipconfig.ipv4_cidr =
      net_base::IPv4CIDR::CreateFromCIDRString(kIPv4CIDR);

  EXPECT_CALL(*target_, StopClat(true));
  target_->OnDefaultLogicalDeviceIPConfigChanged(v6only_dev);

  EXPECT_CALL(*target_, StartClat(_)).Times(Exactly(0));
  target_->Enable();
}

TEST_F(ClatServiceTest, EnabledAfterBecomingIPv6OnlyWhileDisabled) {
  auto dual_dev = MakeFakeDualStackShillDevice("dual");

  target_->OnDefaultLogicalDeviceIPConfigChanged(dual_dev);

  EXPECT_CALL(*target_, StopClat(false));
  target_->Disable();

  dual_dev.ipconfig.ipv4_cidr.reset();

  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("dual")));
  target_->OnDefaultLogicalDeviceIPConfigChanged(dual_dev);

  // The default logical device is IPv6-only, so CLAT starts immdiately after
  // it's enabled.
  EXPECT_CALL(*target_, StartClat(ShillDeviceHasInterfaceName("dual")));
  target_->Enable();
}

TEST_F(ClatServiceTest, VerifyStartAndStopClat) {
  ClatService target(&datapath_, &process_manager_, &system_);
  target.Enable();

  auto v4only_dev = MakeFakeIPv4OnlyShillDevice("v4only", 1);
  auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);

  target.OnShillDefaultLogicalDeviceChanged(&v4only_dev, nullptr);

  EXPECT_CALL(system_, WriteConfigFile).WillOnce(Return(true));
  EXPECT_CALL(
      datapath_,
      AddTunTap(StrEq("tun_nat64"), Eq(std::nullopt),
                net_base::IPv4CIDR::CreateFromCIDRString("192.0.0.1/29"),
                IsEmpty(), DeviceMode::kTun))
      .WillOnce(Return("tun_nat64"));
  EXPECT_CALL(datapath_,
              ModifyClatAcceptRules(Iptables::Command::kA, StrEq("tun_nat64")))
      .WillOnce(Return(true));
  EXPECT_CALL(datapath_, AddIPv6HostRoute(StrEq("tun_nat64"),
                                          CIDRHasPrefix("2001:db8::/64"),
                                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      datapath_,
      AddIPv6NeighborProxy(StrEq("v6only"), AddressHasPrefix("2001:db8::/64")))
      .WillOnce(Return(true));
  EXPECT_CALL(datapath_, AddIPv4RouteToTable(StrEq("tun_nat64"),
                                             net_base::IPv4CIDR(), 249))
      .WillOnce(Return(true));
  // StartClat() is called.
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, &v4only_dev);

  EXPECT_CALL(datapath_, DeleteIPv4RouteFromTable(StrEq("tun_nat64"),
                                                  net_base::IPv4CIDR(), 249));
  EXPECT_CALL(datapath_,
              RemoveIPv6NeighborProxy(StrEq("v6only"),
                                      AddressHasPrefix("2001:db8::/64")));
  EXPECT_CALL(datapath_,
              ModifyClatAcceptRules(Iptables::Command::kD, StrEq("tun_nat64")));
  EXPECT_CALL(datapath_, RemoveIPv6HostRoute(CIDRHasPrefix("2001:db8::/64")));
  EXPECT_CALL(datapath_, RemoveTunTap(StrEq("tun_nat64"), DeviceMode::kTun));
  // StopClat() is called.
  target.OnShillDefaultLogicalDeviceChanged(&v4only_dev, &v6only_dev);
}

TEST_F(ClatServiceTest, CleanUpDatapathWhenDisabled) {
  ClatService target(&datapath_, &process_manager_, &system_);
  target.Enable();

  auto v6only_dev = MakeFakeIPv6OnlyShillDevice("v6only", 2);
  ON_CALL(system_, WriteConfigFile).WillByDefault(Return(true));
  ON_CALL(datapath_, AddTunTap).WillByDefault(Return("tun_nat64"));
  ON_CALL(datapath_, ModifyClatAcceptRules).WillByDefault(Return(true));
  ON_CALL(datapath_, AddIPv6HostRoute).WillByDefault(Return(true));
  ON_CALL(datapath_, AddIPv6NeighborProxy).WillByDefault(Return(true));
  ON_CALL(datapath_, AddIPv4RouteToTable).WillByDefault(Return(true));
  // Start CLAT.
  target.OnShillDefaultLogicalDeviceChanged(&v6only_dev, nullptr);

  EXPECT_CALL(datapath_, DeleteIPv4RouteFromTable(StrEq("tun_nat64"),
                                                  net_base::IPv4CIDR(), 249));
  EXPECT_CALL(datapath_,
              RemoveIPv6NeighborProxy(StrEq("v6only"),
                                      AddressHasPrefix("2001:db8::/64")));
  EXPECT_CALL(datapath_,
              ModifyClatAcceptRules(Iptables::Command::kD, StrEq("tun_nat64")));
  EXPECT_CALL(datapath_, RemoveIPv6HostRoute(CIDRHasPrefix("2001:db8::/64")));
  EXPECT_CALL(datapath_, RemoveTunTap(StrEq("tun_nat64"), DeviceMode::kTun));
  target.Disable();
}

}  // namespace
}  // namespace patchpanel
