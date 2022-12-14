// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/slaac_controller.h"

#include "shill/net/mock_rtnl_handler.h"
#include "shill/net/mock_time.h"
#include "shill/network/mock_network.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace shill {
namespace {

constexpr int kTestIfindex = 123;
constexpr char kTestIfname[] = "eth_test";
constexpr auto kTestTechnology = Technology::kUnknown;

constexpr char kTestIPAddress1[] = "fe80::1aa9:5ff:abcd:1234";
constexpr char kTestIPAddress2[] = "fe80::1aa9:5ff:abcd:1235";
}  // namespace

class SLAACControllerTest : public testing::Test {
 public:
  SLAACControllerTest()
      : slaac_controller_(kTestIfindex, &network_, &rtnl_handler_),
        network_(kTestIfindex, kTestIfname, kTestTechnology) {
    slaac_controller_.time_ = &time_;
  }
  ~SLAACControllerTest() override = default;

  void SendRTNLMessage(const RTNLMessage& message);
  std::unique_ptr<RTNLMessage> BuildRdnssMessage(
      RTNLMessage::Mode mode,
      uint32_t lifetime,
      const std::vector<IPAddress>& dns_servers);

  SLAACController slaac_controller_;
  MockRTNLHandler rtnl_handler_;
  MockNetwork network_;
  MockTime time_;
};

void SLAACControllerTest::SendRTNLMessage(const RTNLMessage& message) {
  if (message.type() == RTNLMessage::kTypeAddress) {
    slaac_controller_.AddressMsgHandler(message);
  } else if (message.type() == RTNLMessage::kTypeRdnss) {
    slaac_controller_.RDNSSMsgHandler(message);
  } else {
    NOTREACHED();
  }
}

std::unique_ptr<RTNLMessage> SLAACControllerTest::BuildRdnssMessage(
    RTNLMessage::Mode mode,
    uint32_t lifetime,
    const std::vector<IPAddress>& dns_servers) {
  auto message =
      std::make_unique<RTNLMessage>(RTNLMessage::kTypeRdnss, mode, 0, 0, 0,
                                    kTestIfindex, IPAddress::kFamilyIPv6);
  message->set_rdnss_option(RTNLMessage::RdnssOption(lifetime, dns_servers));
  return message;
}

TEST_F(SLAACControllerTest, IPv6DnsServerAddressesChanged) {
  std::vector<IPAddress> dns_server_addresses_out;
  uint32_t lifetime_out;

  // No IPv6 dns server addresses.
  EXPECT_FALSE(slaac_controller_.GetIPv6DNSServerAddresses(
      &dns_server_addresses_out, &lifetime_out));

  // Setup IPv6 dns server addresses.
  IPAddress ipv6_address1(IPAddress::kFamilyIPv6);
  IPAddress ipv6_address2(IPAddress::kFamilyIPv6);
  EXPECT_TRUE(ipv6_address1.SetAddressFromString(kTestIPAddress1));
  EXPECT_TRUE(ipv6_address2.SetAddressFromString(kTestIPAddress2));
  std::vector<IPAddress> dns_server_addresses_in = {ipv6_address1,
                                                    ipv6_address2};

  // Infinite lifetime
  const uint32_t kInfiniteLifetime = 0xffffffff;
  auto message = BuildRdnssMessage(RTNLMessage::kModeAdd, kInfiniteLifetime,
                                   dns_server_addresses_in);
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(0), Return(true)));
  EXPECT_CALL(network_, OnIPv6DnsServerAddressesChanged()).Times(1);
  SendRTNLMessage(*message);
  EXPECT_CALL(time_, GetSecondsBoottime(_)).Times(0);
  EXPECT_TRUE(slaac_controller_.GetIPv6DNSServerAddresses(
      &dns_server_addresses_out, &lifetime_out));
  // Verify addresses and lifetime.
  EXPECT_EQ(kInfiniteLifetime, lifetime_out);
  EXPECT_EQ(2, dns_server_addresses_out.size());
  EXPECT_EQ(kTestIPAddress1, dns_server_addresses_out.at(0).ToString());
  EXPECT_EQ(kTestIPAddress2, dns_server_addresses_out.at(1).ToString());

  // Lifetime of 120, retrieve DNS server addresses after 10 seconds.
  const uint32_t kLifetime120 = 120;
  const uint32_t kElapseTime10 = 10;
  auto message1 = BuildRdnssMessage(RTNLMessage::kModeAdd, kLifetime120,
                                    dns_server_addresses_in);
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(0), Return(true)));
  EXPECT_CALL(network_, OnIPv6DnsServerAddressesChanged()).Times(1);
  SendRTNLMessage(*message1);
  // 10 seconds passed when GetIPv6DnsServerAddresses is called.
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(kElapseTime10), Return(true)));
  EXPECT_TRUE(slaac_controller_.GetIPv6DNSServerAddresses(
      &dns_server_addresses_out, &lifetime_out));
  // Verify addresses and lifetime.
  EXPECT_EQ(kLifetime120 - kElapseTime10, lifetime_out);
  EXPECT_EQ(2, dns_server_addresses_out.size());
  EXPECT_EQ(kTestIPAddress1, dns_server_addresses_out.at(0).ToString());
  EXPECT_EQ(kTestIPAddress2, dns_server_addresses_out.at(1).ToString());

  // Lifetime of 120, retrieve DNS server addresses after lifetime expired.
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(0), Return(true)));
  EXPECT_CALL(network_, OnIPv6DnsServerAddressesChanged()).Times(1);
  SendRTNLMessage(*message1);
  // 120 seconds passed when GetIPv6DnsServerAddresses is called.
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(kLifetime120), Return(true)));
  EXPECT_TRUE(slaac_controller_.GetIPv6DNSServerAddresses(
      &dns_server_addresses_out, &lifetime_out));
  // Verify addresses and lifetime.
  EXPECT_EQ(0, lifetime_out);
  EXPECT_EQ(2, dns_server_addresses_out.size());
  EXPECT_EQ(kTestIPAddress1, dns_server_addresses_out.at(0).ToString());
  EXPECT_EQ(kTestIPAddress2, dns_server_addresses_out.at(1).ToString());
}

}  // namespace shill
