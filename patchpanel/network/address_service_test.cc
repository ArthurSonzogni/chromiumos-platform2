// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/address_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/mock_proc_fs_stub.h>
#include <net-base/mock_rtnl_handler.h>
#include <net-base/network_priority.h>

using testing::_;
using testing::Eq;
using testing::Return;
using testing::StrictMock;

namespace patchpanel {

class AddressServiceTest : public testing::Test {
 public:
  AddressServiceTest() {
    address_service_ = AddressService::CreateForTesting(&address_rtnl_handler_);
  }

 protected:
  StrictMock<net_base::MockRTNLHandler> address_rtnl_handler_;
  std::unique_ptr<AddressService> address_service_;
};

TEST_F(AddressServiceTest, AddAddressFlow) {
  const int kInterfaceIndex = 3;
  const auto ipv4_addr_1 =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.2/24");
  const auto ipv4_addr_2 =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.2.2/24");
  const auto ipv6_addr_1 =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::abcd/64");

  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv4_addr_1),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv4Address(kInterfaceIndex, ipv4_addr_1, std::nullopt);

  // Setting a second IPv4 address should remove the first one.
  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv4_addr_1)));
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv4_addr_2),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv4Address(kInterfaceIndex, ipv4_addr_2, std::nullopt);

  // Setting an IPv6 address will not remove the IPv4 one.
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv6_addr_1),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv6Addresses(kInterfaceIndex, {ipv6_addr_1});

  // Similarly adding an IPv4 address will not remove the IPv6 one.
  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv4_addr_2)));
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv4_addr_1),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv4Address(kInterfaceIndex, ipv4_addr_1, std::nullopt);

  // ClearIPv4Address will remove the previous added IPv4 address.
  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv4_addr_1)));
  address_service_->ClearIPv4Address(kInterfaceIndex);
}

TEST_F(AddressServiceTest, IPv4WithBroadcast) {
  const int kInterfaceIndex = 3;
  const auto local =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.2/24");
  const auto broadcast =
      net_base::IPv4Address::CreateFromString("192.168.1.200");

  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(local), broadcast))
      .WillOnce(Return(true));
  address_service_->SetIPv4Address(kInterfaceIndex, local, broadcast);
}

TEST_F(AddressServiceTest, MultiIPv6Address) {
  const int kInterfaceIndex = 3;
  const auto ipv6_addr_1 =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::abcd/64");
  const auto ipv6_addr_2 =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::1234/64");
  const auto ipv6_addr_3 =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::5678/64");

  // Setting two IPv6 addresses.
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv6_addr_1),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv6_addr_2),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv6Addresses(kInterfaceIndex,
                                     {ipv6_addr_1, ipv6_addr_2});

  // Setting another two instead. The shared address won't be touched.
  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv6_addr_1)));
  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv6_addr_3),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv6Addresses(kInterfaceIndex,
                                     {ipv6_addr_2, ipv6_addr_3});

  // Clearing the addresses.
  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv6_addr_2)));
  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex, net_base::IPCIDR(ipv6_addr_3)));
  address_service_->SetIPv6Addresses(kInterfaceIndex, {});
}

TEST_F(AddressServiceTest, MultipleInterface) {
  const int kInterfaceIndex1 = 3;
  const int kInterfaceIndex2 = 7;
  const auto ipv4_addr_1 =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.1.2/24");
  const auto ipv4_addr_2 =
      *net_base::IPv4CIDR::CreateFromCIDRString("192.168.2.2/24");
  const auto ipv6_addr_1 =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::abcd/64");
  const auto ipv6_addr_2 =
      *net_base::IPv6CIDR::CreateFromCIDRString("2001:db8:0:100::1234/64");

  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex1, net_base::IPCIDR(ipv4_addr_1),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv4Address(kInterfaceIndex1, ipv4_addr_1);

  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex2, net_base::IPCIDR(ipv4_addr_2),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv4Address(kInterfaceIndex2, ipv4_addr_2);

  // Clearing interface 2 should has no effect on interface 1.
  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex2, net_base::IPCIDR(ipv4_addr_2)));
  address_service_->ClearIPv4Address(kInterfaceIndex2);

  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex1, net_base::IPCIDR(ipv6_addr_1),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv6Addresses(kInterfaceIndex1, {ipv6_addr_1});

  EXPECT_CALL(
      address_rtnl_handler_,
      AddInterfaceAddress(kInterfaceIndex2, net_base::IPCIDR(ipv6_addr_2),
                          Eq(std::nullopt)))
      .WillOnce(Return(true));
  address_service_->SetIPv6Addresses(kInterfaceIndex2, {ipv6_addr_2});

  EXPECT_CALL(
      address_rtnl_handler_,
      RemoveInterfaceAddress(kInterfaceIndex2, net_base::IPCIDR(ipv6_addr_2)));
  address_service_->SetIPv6Addresses(kInterfaceIndex2, {});
}

}  // namespace patchpanel
