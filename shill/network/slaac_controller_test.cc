// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/slaac_controller.h"

#include <string>

#include <net-base/byte_utils.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/mock_rtnl_handler.h>
#include <net-base/rtnl_message.h>

#include "shill/network/mock_network.h"
#include "shill/network/mock_proc_fs_stub.h"
#include "shill/test_event_dispatcher.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace shill {
namespace {

constexpr int kTestIfindex = 123;
constexpr char kTestIfname[] = "eth_test";
constexpr auto kTestTechnology = Technology::kUnknown;

constexpr net_base::IPv4Address kTestIPAddress0(192, 168, 1, 1);
const net_base::IPv6Address kTestIPAddress1 =
    *net_base::IPv6Address::CreateFromString("fe80::1aa9:5ff:abcd:1234");
const net_base::IPv6Address kTestIPAddress2 =
    *net_base::IPv6Address::CreateFromString("fe80::1aa9:5ff:abcd:1235");
const net_base::IPv6Address kTestIPAddress3 =
    *net_base::IPv6Address::CreateFromString("fe80::1aa9:5ff:abcd:1236");
const net_base::IPv6Address kTestIPAddress4 =
    *net_base::IPv6Address::CreateFromString("fe80::1aa9:5ff:abcd:1237");
const net_base::IPv6Address kTestIPAddress7 =
    *net_base::IPv6Address::CreateFromString("fe80::1aa9:5ff:abcd:1238");
}  // namespace

class SLAACControllerTest : public testing::Test {
 public:
  SLAACControllerTest()
      : slaac_controller_(
            kTestIfindex, &proc_fs_, &rtnl_handler_, &dispatcher_),
        proc_fs_(kTestIfname),
        network_(kTestIfindex, kTestIfname, kTestTechnology) {}
  ~SLAACControllerTest() override = default;

  void SetUp() override {
    slaac_controller_.RegisterCallback(base::BindRepeating(
        &SLAACControllerTest::UpdateCallback, base::Unretained(this)));
  }

  void SendRTNLMessage(const net_base::RTNLMessage& message);
  std::unique_ptr<net_base::RTNLMessage> BuildRdnssMessage(
      net_base::RTNLMessage::Mode mode,
      uint32_t lifetime,
      const std::vector<net_base::IPv6Address>& dns_servers);
  std::unique_ptr<net_base::RTNLMessage> BuildDnsslMessage(
      net_base::RTNLMessage::Mode mode,
      uint32_t lifetime,
      const std::vector<std::string>& domains);
  std::unique_ptr<net_base::RTNLMessage> BuildAddressMessage(
      net_base::RTNLMessage::Mode mode,
      const net_base::IPCIDR& address,
      unsigned char flags,
      unsigned char scope);

  MOCK_METHOD(void, UpdateCallback, (SLAACController::UpdateType));

  SLAACController slaac_controller_;
  MockProcFsStub proc_fs_;
  net_base::MockRTNLHandler rtnl_handler_;
  MockNetwork network_;
  EventDispatcherForTest dispatcher_;
};

void SLAACControllerTest::SendRTNLMessage(
    const net_base::RTNLMessage& message) {
  if (message.type() == net_base::RTNLMessage::kTypeAddress) {
    slaac_controller_.AddressMsgHandler(message);
  } else if (message.type() == net_base::RTNLMessage::kTypeRoute) {
    slaac_controller_.RouteMsgHandler(message);
  } else if (message.type() == net_base::RTNLMessage::kTypeRdnss ||
             message.type() == net_base::RTNLMessage::kTypeDnssl) {
    slaac_controller_.NDOptionMsgHandler(message);
  } else {
    NOTREACHED();
  }
}

std::unique_ptr<net_base::RTNLMessage> SLAACControllerTest::BuildRdnssMessage(
    net_base::RTNLMessage::Mode mode,
    uint32_t lifetime,
    const std::vector<net_base::IPv6Address>& dns_servers) {
  auto message = std::make_unique<net_base::RTNLMessage>(
      net_base::RTNLMessage::kTypeRdnss, mode, 0, 0, 0, kTestIfindex, AF_INET6);
  message->set_rdnss_option(
      net_base::RTNLMessage::RdnssOption(lifetime, dns_servers));
  return message;
}

std::unique_ptr<net_base::RTNLMessage> SLAACControllerTest::BuildDnsslMessage(
    net_base::RTNLMessage::Mode mode,
    uint32_t lifetime,
    const std::vector<std::string>& domains) {
  auto message = std::make_unique<net_base::RTNLMessage>(
      net_base::RTNLMessage::kTypeDnssl, mode, 0, 0, 0, kTestIfindex, AF_INET6);
  net_base::RTNLMessage::DnsslOption dnssl_option;
  dnssl_option.lifetime = lifetime;
  dnssl_option.domains = domains;
  message->set_dnssl_option(dnssl_option);
  return message;
}

std::unique_ptr<net_base::RTNLMessage> SLAACControllerTest::BuildAddressMessage(
    net_base::RTNLMessage::Mode mode,
    const net_base::IPCIDR& cidr,
    unsigned char flags,
    unsigned char scope) {
  auto message = std::make_unique<net_base::RTNLMessage>(
      net_base::RTNLMessage::kTypeAddress, mode, 0, 0, 0, kTestIfindex,
      net_base::ToSAFamily(cidr.GetFamily()));
  message->SetAttribute(IFA_ADDRESS, cidr.address().ToBytes());
  message->set_address_status(
      net_base::RTNLMessage::AddressStatus(cidr.prefix_length(), flags, scope));
  return message;
}

TEST_F(SLAACControllerTest, IPv6DnsServerAddressesChanged) {
  // No IPv6 dns server addresses.
  auto network_config_out = slaac_controller_.GetNetworkConfig();
  EXPECT_EQ(0, network_config_out.dns_servers.size());

  // Setup IPv6 dns server addresses.
  std::vector<net_base::IPv6Address> dns_server_addresses_in = {
      kTestIPAddress1,
      kTestIPAddress2,
  };
  std::vector<net_base::IPAddress> dns_server_addresses_expected_out = {
      net_base::IPAddress(kTestIPAddress1),
      net_base::IPAddress(kTestIPAddress2),
  };

  // Infinite lifetime
  const uint32_t kInfiniteLifetime = 0xffffffff;
  auto message = BuildRdnssMessage(net_base::RTNLMessage::kModeAdd,
                                   kInfiniteLifetime, dns_server_addresses_in);

  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kRDNSS))
      .Times(1);
  SendRTNLMessage(*message);
  network_config_out = slaac_controller_.GetNetworkConfig();
  // Verify addresses.
  EXPECT_EQ(dns_server_addresses_expected_out, network_config_out.dns_servers);

  // Lifetime of 0
  const uint32_t kLifetime0 = 0;
  auto message2 = BuildRdnssMessage(net_base::RTNLMessage::kModeAdd, kLifetime0,
                                    dns_server_addresses_in);
  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kRDNSS))
      .Times(1);
  SendRTNLMessage(*message2);

  network_config_out = slaac_controller_.GetNetworkConfig();
  // Verify addresses.
  EXPECT_EQ(0, network_config_out.dns_servers.size());

  // Lifetime of 120
  const uint32_t kLifetime120 = 120;
  auto message1 = BuildRdnssMessage(net_base::RTNLMessage::kModeAdd,
                                    kLifetime120, dns_server_addresses_in);
  EXPECT_CALL(*this, UpdateCallback(
                         SLAACController::SLAACController::UpdateType::kRDNSS))
      .Times(1);
  SendRTNLMessage(*message1);

  network_config_out = slaac_controller_.GetNetworkConfig();
  // Verify addresses.
  EXPECT_EQ(dns_server_addresses_expected_out, network_config_out.dns_servers);
}

TEST_F(SLAACControllerTest, DNSSL) {
  auto network_config_out = slaac_controller_.GetNetworkConfig();
  EXPECT_EQ(0, network_config_out.dns_search_domains.size());

  std::vector<std::string> dnssl_in = {"foo.bar", "foo.2.bar"};

  // Infinite lifetime
  const uint32_t kInfiniteLifetime = 0xffffffff;
  auto message = BuildDnsslMessage(net_base::RTNLMessage::kModeAdd,
                                   kInfiniteLifetime, dnssl_in);

  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kDNSSL))
      .Times(1);
  SendRTNLMessage(*message);
  network_config_out = slaac_controller_.GetNetworkConfig();
  EXPECT_EQ(dnssl_in, network_config_out.dns_search_domains);
}

TEST_F(SLAACControllerTest, IPv6AddressChanged) {
  // Contains no addresses.
  EXPECT_TRUE(slaac_controller_.GetNetworkConfig().ipv6_addresses.empty());

  auto message = BuildAddressMessage(net_base::RTNLMessage::kModeAdd,
                                     net_base::IPCIDR(kTestIPAddress0), 0, 0);

  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kAddress))
      .Times(0);

  // We should ignore IPv4 addresses.
  SendRTNLMessage(*message);
  EXPECT_TRUE(slaac_controller_.GetNetworkConfig().ipv6_addresses.empty());

  message =
      BuildAddressMessage(net_base::RTNLMessage::kModeAdd,
                          net_base::IPCIDR(kTestIPAddress1), 0, RT_SCOPE_LINK);

  // We should ignore non-SCOPE_UNIVERSE messages for IPv6.
  SendRTNLMessage(*message);
  EXPECT_TRUE(slaac_controller_.GetNetworkConfig().ipv6_addresses.empty());

  message = BuildAddressMessage(net_base::RTNLMessage::kModeAdd,
                                net_base::IPCIDR(kTestIPAddress2),
                                IFA_F_TEMPORARY, RT_SCOPE_UNIVERSE);

  // Add a temporary address.
  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kAddress))
      .Times(1);
  SendRTNLMessage(*message);
  EXPECT_EQ(
      slaac_controller_.GetNetworkConfig().ipv6_addresses,
      std::vector<net_base::IPv6CIDR>({net_base::IPv6CIDR(kTestIPAddress2)}));

  message = BuildAddressMessage(net_base::RTNLMessage::kModeAdd,
                                net_base::IPCIDR(kTestIPAddress3), 0,
                                RT_SCOPE_UNIVERSE);

  // Adding a non-temporary address alerts the Device, but does not override
  // the primary address since the previous one was temporary.
  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kAddress))
      .Times(1);
  SendRTNLMessage(*message);
  EXPECT_EQ(
      slaac_controller_.GetNetworkConfig().ipv6_addresses,
      std::vector<net_base::IPv6CIDR>({net_base::IPv6CIDR(kTestIPAddress2),
                                       net_base::IPv6CIDR(kTestIPAddress3)}));

  message = BuildAddressMessage(
      net_base::RTNLMessage::kModeAdd, net_base::IPCIDR(kTestIPAddress4),
      IFA_F_TEMPORARY | IFA_F_DEPRECATED, RT_SCOPE_UNIVERSE);

  // Adding a temporary deprecated address alerts the Device, but does not
  // override the primary address since the previous one was non-deprecated.
  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kAddress))
      .Times(1);
  SendRTNLMessage(*message);
  EXPECT_EQ(
      slaac_controller_.GetNetworkConfig().ipv6_addresses,
      std::vector<net_base::IPv6CIDR>({net_base::IPv6CIDR(kTestIPAddress2),
                                       net_base::IPv6CIDR(kTestIPAddress3),
                                       net_base::IPv6CIDR(kTestIPAddress4)}));

  message = BuildAddressMessage(net_base::RTNLMessage::kModeAdd,
                                net_base::IPCIDR(kTestIPAddress7),
                                IFA_F_TEMPORARY, RT_SCOPE_UNIVERSE);

  // Another temporary (non-deprecated) address alerts the Device, and will
  // override the previous primary address.
  EXPECT_CALL(*this, UpdateCallback(SLAACController::UpdateType::kAddress))
      .Times(1);
  SendRTNLMessage(*message);
  EXPECT_EQ(
      slaac_controller_.GetNetworkConfig().ipv6_addresses,
      std::vector<net_base::IPv6CIDR>({net_base::IPv6CIDR(kTestIPAddress7),
                                       net_base::IPv6CIDR(kTestIPAddress2),
                                       net_base::IPv6CIDR(kTestIPAddress3),
                                       net_base::IPv6CIDR(kTestIPAddress4)}));
}

TEST_F(SLAACControllerTest, StartIPv6Flags) {
  EXPECT_CALL(proc_fs_, SetIPFlag(net_base::IPFamily::kIPv6, "accept_dad", "1"))
      .WillOnce(Return(true));
  EXPECT_CALL(proc_fs_,
              SetIPFlag(net_base::IPFamily::kIPv6, "use_tempaddr", "2"))
      .WillOnce(Return(true));
  testing::Expectation accept_ra =
      EXPECT_CALL(proc_fs_,
                  SetIPFlag(net_base::IPFamily::kIPv6, "accept_ra", "2"))
          .WillOnce(Return(true));
  testing::Expectation addr_gen_mode =
      EXPECT_CALL(proc_fs_,
                  SetIPFlag(net_base::IPFamily::kIPv6, "addr_gen_mode", "0"))
          .WillOnce(Return(true));

  testing::Expectation disable_ipv6 =
      EXPECT_CALL(proc_fs_,
                  SetIPFlag(net_base::IPFamily::kIPv6, "disable_ipv6", "1"))
          .WillOnce(Return(true));
  EXPECT_CALL(proc_fs_,
              SetIPFlag(net_base::IPFamily::kIPv6, "disable_ipv6", "0"))
      .After(accept_ra, addr_gen_mode, disable_ipv6)
      .WillOnce(Return(true));

  slaac_controller_.Start();
}

TEST_F(SLAACControllerTest, StartIPv6FlagsWithLinkLocal) {
  EXPECT_CALL(proc_fs_, SetIPFlag(net_base::IPFamily::kIPv6, "accept_dad", "1"))
      .WillOnce(Return(true));
  EXPECT_CALL(proc_fs_,
              SetIPFlag(net_base::IPFamily::kIPv6, "use_tempaddr", "2"))
      .WillOnce(Return(true));
  testing::Expectation accept_ra =
      EXPECT_CALL(proc_fs_,
                  SetIPFlag(net_base::IPFamily::kIPv6, "accept_ra", "2"))
          .WillOnce(Return(true));
  testing::Expectation addr_gen_mode =
      EXPECT_CALL(proc_fs_,
                  SetIPFlag(net_base::IPFamily::kIPv6, "addr_gen_mode", "1"))
          .WillOnce(Return(true));

  testing::Expectation disable_ipv6 =
      EXPECT_CALL(proc_fs_,
                  SetIPFlag(net_base::IPFamily::kIPv6, "disable_ipv6", "1"))
          .WillOnce(Return(true));
  testing::Expectation reenable_ipv6 =
      EXPECT_CALL(proc_fs_,
                  SetIPFlag(net_base::IPFamily::kIPv6, "disable_ipv6", "0"))
          .After(accept_ra, addr_gen_mode, disable_ipv6)
          .WillOnce(Return(true));
  EXPECT_CALL(rtnl_handler_, AddInterfaceAddress(_, _, _))
      .After(reenable_ipv6)
      .WillOnce(Return(true));

  slaac_controller_.Start(net_base::IPv6Address::CreateFromString("fe80::5"));
}

}  // namespace shill
