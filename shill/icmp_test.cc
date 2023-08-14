// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/icmp.h"

#include <netinet/in.h>
#include <netinet/ip_icmp.h>

#include <utility>

#include <gtest/gtest.h>
#include <net-base/mock_socket.h>

#include "shill/mock_log.h"

using testing::_;
using testing::HasSubstr;
using testing::InSequence;
using testing::Return;
using testing::StrictMock;
using testing::Test;

namespace shill {
namespace {

constexpr int kInterfaceIndex = 3;
// These binary blobs representing ICMP headers and their respective checksums
// were taken directly from Wireshark ICMP packet captures and are given in big
// endian. The checksum field is zeroed in |kIcmpEchoRequestEvenLen| and
// |kIcmpEchoRequestOddLen| so the checksum can be calculated on the header in
// IcmpTest.ComputeIcmpChecksum.
alignas(struct icmphdr) const uint8_t kIcmpEchoRequestEvenLen[] = {
    0x08, 0x00, 0x00, 0x00, 0x71, 0x50, 0x00, 0x00};
alignas(struct icmphdr) const uint8_t kIcmpEchoRequestEvenLenChecksum[] = {
    0x86, 0xaf};
alignas(struct icmphdr) const uint8_t kIcmpEchoRequestOddLen[] = {
    0x08, 0x00, 0x00, 0x00, 0xac, 0x51, 0x00, 0x00, 0x00, 0x00, 0x01};
const uint8_t kIcmpEchoRequestOddLenChecksum[] = {0x4a, 0xae};

const net_base::IPAddress kIPAddress =
    *net_base::IPAddress::CreateFromString("10.0.1.1");

}  // namespace

class IcmpTest : public Test {
 public:
  void TearDown() override {
    if (icmp_.IsStarted()) {
      icmp_.Stop();
    }
    EXPECT_FALSE(icmp_.IsStarted());
  }

  // Set the socket factory method into |icmp_|.
  // |method| is a lambda function that creates a net_base::MockSocket.
  void SetSocketFactory(
      int expected_domain,
      int expected_type,
      int expected_protocol,
      std::function<std::unique_ptr<net_base::MockSocket>()> method) {
    icmp_.socket_factory_ = base::BindRepeating(
        [](int expected_domain, int expected_type, int expected_protocol,
           std::function<std::unique_ptr<net_base::MockSocket>()> method,
           int domain, int type,
           int protocol) -> std::unique_ptr<net_base::Socket> {
          EXPECT_EQ(domain, expected_domain);
          EXPECT_EQ(type, expected_type);
          EXPECT_EQ(protocol, expected_protocol);

          return method();
        },
        expected_domain, expected_type, expected_protocol, std::move(method));
  }

 protected:
  uint16_t ComputeIcmpChecksum(const struct icmphdr& hdr, size_t len) {
    return Icmp::ComputeIcmpChecksum(hdr, len);
  }

  Icmp icmp_;
};

TEST_F(IcmpTest, Constructor) {
  EXPECT_EQ(-1, icmp_.socket());
  EXPECT_FALSE(icmp_.IsStarted());
}

TEST_F(IcmpTest, SocketOpenFail) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                       HasSubstr("Could not create ICMP socket")))
      .Times(1);

  SetSocketFactory(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP,
                   []() { return nullptr; });

  EXPECT_FALSE(icmp_.Start(kIPAddress, kInterfaceIndex));
  EXPECT_FALSE(icmp_.IsStarted());
}

TEST_F(IcmpTest, SocketNonBlockingFail) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                       HasSubstr("Could not set socket to be non-blocking")))
      .Times(1);

  SetSocketFactory(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP, []() {
    auto socket = std::make_unique<net_base::MockSocket>();
    EXPECT_CALL(*socket, SetNonBlocking()).WillOnce(Return(false));
    return socket;
  });

  EXPECT_FALSE(icmp_.Start(kIPAddress, kInterfaceIndex));
  EXPECT_FALSE(icmp_.IsStarted());
}

TEST_F(IcmpTest, StartMultipleTimes) {
  SetSocketFactory(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP, []() {
    auto socket = std::make_unique<net_base::MockSocket>();
    EXPECT_CALL(*socket, SetNonBlocking()).WillOnce(Return(true));
    return socket;
  });

  EXPECT_TRUE(icmp_.Start(kIPAddress, kInterfaceIndex));
  EXPECT_TRUE(icmp_.IsStarted());

  EXPECT_TRUE(icmp_.Start(kIPAddress, kInterfaceIndex));
  EXPECT_TRUE(icmp_.IsStarted());
}

MATCHER_P(IsIcmpHeader, header, "") {
  return memcmp(arg.data(), &header, arg.size()) == 0;
}

MATCHER_P(IsSocketAddress, address, "") {
  const struct sockaddr_in* sock_addr =
      reinterpret_cast<const struct sockaddr_in*>(arg);
  const auto addr_bytes = address.ToByteString();
  return sock_addr->sin_family == net_base::ToSAFamily(address.GetFamily()) &&
         memcmp(&sock_addr->sin_addr.s_addr, addr_bytes.data(),
                addr_bytes.size()) == 0;
}

TEST_F(IcmpTest, TransmitEchoRequest) {
  struct icmphdr icmp_header;
  memset(&icmp_header, 0, sizeof(icmp_header));
  icmp_header.type = ICMP_ECHO;
  icmp_header.code = Icmp::kIcmpEchoCode;
  icmp_header.un.echo.id = 1;
  icmp_header.un.echo.sequence = 1;
  icmp_header.checksum = ComputeIcmpChecksum(icmp_header, sizeof(icmp_header));

  SetSocketFactory(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP, [=]() {
    auto socket = std::make_unique<net_base::MockSocket>();
    EXPECT_CALL(*socket, SetNonBlocking()).WillOnce(Return(true));

    EXPECT_CALL(*socket,
                SendTo(IsIcmpHeader(icmp_header), 0,
                       IsSocketAddress(kIPAddress), sizeof(sockaddr_in)))
        .WillOnce(Return(std::nullopt))
        .WillOnce(Return(0))
        .WillOnce(Return(sizeof(icmp_header) - 1))
        .WillOnce(Return(sizeof(icmp_header)));
    return socket;
  });

  // Address isn't valid.
  EXPECT_FALSE(icmp_.TransmitEchoRequest(1, 1));
  EXPECT_TRUE(icmp_.Start(kIPAddress, kInterfaceIndex));
  EXPECT_TRUE(icmp_.IsStarted());

  {
    InSequence seq;
    ScopedMockLog log;
    EXPECT_CALL(
        log, Log(logging::LOGGING_ERROR, _, HasSubstr("Socket sendto failed")))
        .Times(1);
    EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                         HasSubstr("less than the expected result")))
        .Times(2);

    EXPECT_FALSE(icmp_.TransmitEchoRequest(1, 1));
    EXPECT_FALSE(icmp_.TransmitEchoRequest(1, 1));
    EXPECT_FALSE(icmp_.TransmitEchoRequest(1, 1));
    EXPECT_TRUE(icmp_.TransmitEchoRequest(1, 1));
  }
}

TEST_F(IcmpTest, ComputeIcmpChecksum) {
  EXPECT_EQ(*reinterpret_cast<const uint16_t*>(kIcmpEchoRequestEvenLenChecksum),
            ComputeIcmpChecksum(*reinterpret_cast<const struct icmphdr*>(
                                    kIcmpEchoRequestEvenLen),
                                sizeof(kIcmpEchoRequestEvenLen)));
  EXPECT_EQ(*reinterpret_cast<const uint16_t*>(kIcmpEchoRequestOddLenChecksum),
            ComputeIcmpChecksum(*reinterpret_cast<const struct icmphdr*>(
                                    kIcmpEchoRequestOddLen),
                                sizeof(kIcmpEchoRequestOddLen)));
}

}  // namespace shill
