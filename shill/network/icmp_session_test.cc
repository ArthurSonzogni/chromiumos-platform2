// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/icmp_session.h"

#include <netinet/icmp6.h>

#include <algorithm>
#include <iterator>
#include <memory>

#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/byte_utils.h>
#include <net-base/mock_socket.h>

#include "shill/event_dispatcher.h"

namespace shill {
namespace {

using testing::_;
using testing::ElementsAreArray;
using testing::Return;

// These binary blobs representing ICMP headers and their respective checksums
// were taken directly from Wireshark ICMP packet captures and are given in big
// endian. The checksum field is zeroed in |kIcmpEchoRequestEvenLen| and
// |kIcmpEchoRequestOddLen| so the checksum can be calculated on the header in
// IcmpTest.ComputeIcmpChecksum.
alignas(struct icmphdr) constexpr uint8_t kIcmpEchoRequestEvenLen[] = {
    0x08, 0x00, 0x00, 0x00, 0x71, 0x50, 0x00, 0x00};
alignas(struct icmphdr) constexpr uint8_t kIcmpEchoRequestEvenLenChecksum[] = {
    0x86, 0xaf};
alignas(struct icmphdr) constexpr uint8_t kIcmpEchoRequestOddLen[] = {
    0x08, 0x00, 0x00, 0x00, 0xac, 0x51, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kIcmpEchoRequestOddLenChecksum[] = {0x4a, 0xae};

// The echo ID of the ICMP session.
constexpr int kEchoId = 0;
// Note: this IPv4 header is given in network byte order, since
// IcmpSession::OnEchoReplyReceived expects to receive a raw IP packet.
constexpr uint8_t kIpHeader[] = {0x45, 0x80, 0x00, 0x1c, 0x63, 0xd3, 0x00,
                                 0x00, 0x39, 0x01, 0xcc, 0x9f, 0x4a, 0x7d,
                                 0xe0, 0x18, 0x64, 0x6e, 0xc1, 0xea};
// ICMPv4 echo replies with 0 bytes of data and and echo ID 0. Sequence numbers
// are 0x0, 0x1, and 0x2 respectively to simulate replies to a sequence of sent
// echo requests.  Note that these only match on little-endian hosts.
constexpr uint8_t kIcmpEchoReply1[] = {0x00, 0x00, 0xf7, 0xff,
                                       0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kIcmpEchoReply2[] = {0x00, 0x00, 0xf6, 0xff,
                                       0x00, 0x00, 0x01, 0x00};
constexpr uint8_t kIcmpEchoReply3[] = {0x00, 0x00, 0xf5, 0xff,
                                       0x00, 0x00, 0x02, 0x00};

// ICMPv6 echo reply with 0 bytes of data, echo ID 0, and sequence number 0x0,
// 0x1, and 0x2.
constexpr uint8_t kIcmpV6EchoReply1[] = {0x81, 0x00, 0x76, 0xff,
                                         0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kIcmpV6EchoReply2[] = {0x81, 0x00, 0x75, 0xff,
                                         0x00, 0x00, 0x01, 0x00};
constexpr uint8_t kIcmpV6EchoReply3[] = {0x81, 0x00, 0x74, 0xff,
                                         0x00, 0x00, 0x02, 0x00};

// This ICMPv4 echo reply has an echo ID of 0xe, which is different from the
// echo ID used in the unit tests (0).
constexpr uint8_t kIcmpEchoReplyDifferentEchoID[] = {0x00, 0x00, 0xea, 0xff,
                                                     0x0e, 0x00, 0x0b, 0x00};

const net_base::IPAddress kIPv4Address =
    *net_base::IPAddress::CreateFromString("10.0.1.1");
const net_base::IPAddress kIPv6Address =
    *net_base::IPAddress::CreateFromString("2001:db8::1234:5678");
constexpr int kInterfaceIndex = 3;

std::vector<uint8_t> ConcatBuffers(base::span<const uint8_t> a,
                                   base::span<const uint8_t> b) {
  std::vector<uint8_t> ret;
  ret.reserve(a.size() + b.size());
  std::copy(a.begin(), a.end(), std::back_inserter(ret));
  std::copy(b.begin(), b.end(), std::back_inserter(ret));
  return ret;
}

class IcmpSessionTest : public testing::Test {
 public:
  void SetUp() override {
    auto socket_factory = std::make_unique<net_base::MockSocketFactory>();
    socket_factory_ = socket_factory.get();

    icmp_session_ = IcmpSession::CreateForTesting(
        &dispatcher_, std::move(socket_factory), kEchoId);
  }

  MOCK_METHOD(void, ResultCallback, (const IcmpSession::IcmpSessionResult&));

 protected:
  base::test::TaskEnvironment task_environment_{
      // required by base::FileDescriptorWatcher.
      base::test::TaskEnvironment::MainThreadType::IO,
      // required by FastForwardBy().
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
  };
  EventDispatcher dispatcher_;

  std::unique_ptr<IcmpSession> icmp_session_;
  net_base::MockSocketFactory* socket_factory_;  // Owned by |icmp_session_|.
};

TEST_F(IcmpSessionTest, EchoId) {
  // Each IcmpSession should have different echo ID.
  IcmpSession session1(&dispatcher_);
  IcmpSession session2(&dispatcher_);
  EXPECT_NE(session1.GetEchoIdForTesting(), session2.GetEchoIdForTesting());
}

TEST_F(IcmpSessionTest, SocketOpenFail) {
  EXPECT_CALL(*socket_factory_,
              Create(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP))
      .WillOnce(Return(nullptr));

  EXPECT_FALSE(
      icmp_session_->Start(kIPv4Address, kInterfaceIndex, base::DoNothing()));
  EXPECT_FALSE(icmp_session_->IsStarted());
}

MATCHER_P2(IsIcmpHeader, echo_id, sequence, "") {
  struct icmphdr icmp_header;
  memset(&icmp_header, 0, sizeof(icmp_header));
  icmp_header.type = ICMP_ECHO;
  icmp_header.code = IcmpSession::kIcmpEchoCode;
  icmp_header.un.echo.id = echo_id;
  icmp_header.un.echo.sequence = sequence;
  icmp_header.checksum =
      IcmpSession::ComputeIcmpChecksum(icmp_header, sizeof(icmp_header));

  return arg.size() == sizeof(icmp_header) &&
         memcmp(arg.data(), &icmp_header, arg.size()) == 0;
}

MATCHER_P(IsSocketAddressV4, address, "") {
  const struct sockaddr_in* sock_addr =
      reinterpret_cast<const struct sockaddr_in*>(arg);
  const auto addr_bytes = address.ToByteString();
  return sock_addr->sin_family == net_base::ToSAFamily(address.GetFamily()) &&
         memcmp(&sock_addr->sin_addr.s_addr, addr_bytes.data(),
                addr_bytes.size()) == 0;
}

MATCHER_P(IsSocketAddressV6, address, "") {
  const struct sockaddr_in6* sock_addr =
      reinterpret_cast<const struct sockaddr_in6*>(arg);
  const auto addr_bytes = address.ToByteString();
  return sock_addr->sin6_family == net_base::ToSAFamily(address.GetFamily()) &&
         memcmp(&sock_addr->sin6_addr, addr_bytes.data(), addr_bytes.size()) ==
             0;
}

TEST_F(IcmpSessionTest, SessionSuccess) {
  // Test a successful ICMP session where the sending of requests and receiving
  // of replies are interleaved. Moreover, test the case where transmitting an
  // echo request fails.
  const std::vector<base::TimeDelta> result = {base::Milliseconds(100),
                                               base::Milliseconds(300),
                                               base::Milliseconds(500)};
  EXPECT_CALL(*this, ResultCallback(ElementsAreArray(result)));

  net_base::MockSocket* mock_socket = nullptr;
  EXPECT_CALL(*socket_factory_,
              Create(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP))
      .WillOnce([&mock_socket]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        mock_socket = socket.get();
        return socket;
      });

  EXPECT_TRUE(
      icmp_session_->Start(kIPv4Address, kInterfaceIndex,
                           base::BindOnce(&IcmpSessionTest::ResultCallback,
                                          base::Unretained(this))));
  // When the session is started, Start() should fail.
  EXPECT_FALSE(
      icmp_session_->Start(kIPv4Address, kInterfaceIndex, base::DoNothing()));

  // Send 1st request immediately (T + 0s).
  EXPECT_CALL(*mock_socket,
              SendTo(IsIcmpHeader(kEchoId, 0), 0,
                     IsSocketAddressV4(kIPv4Address), sizeof(sockaddr_in)))
      .WillOnce(Return(sizeof(struct icmphdr)));
  task_environment_.RunUntilIdle();

  // Receive 1st response (T + 0.1s).
  task_environment_.FastForwardBy(result[0]);
  icmp_session_->OnEchoReplyReceivedForTesting(
      ConcatBuffers(kIpHeader, kIcmpEchoReply1));

  // Send 2nd request (T + 1s).
  EXPECT_CALL(*mock_socket,
              SendTo(IsIcmpHeader(kEchoId, 1), 0,
                     IsSocketAddressV4(kIPv4Address), sizeof(sockaddr_in)))
      .WillOnce(Return(sizeof(struct icmphdr)));
  task_environment_.FastForwardBy(IcmpSession::kEchoRequestInterval -
                                  result[0]);

  // Receive 2nd response (T + 1.3s).
  task_environment_.FastForwardBy(result[1]);
  icmp_session_->OnEchoReplyReceivedForTesting(
      ConcatBuffers(kIpHeader, kIcmpEchoReply2));

  // Receive a wrong response (T + 1.3s).
  icmp_session_->OnEchoReplyReceivedForTesting(
      ConcatBuffers(kIpHeader, kIcmpEchoReplyDifferentEchoID));

  // Send 3rd request (T + 2s).
  EXPECT_CALL(*mock_socket,
              SendTo(IsIcmpHeader(kEchoId, 2), 0,
                     IsSocketAddressV4(kIPv4Address), sizeof(sockaddr_in)))
      .WillOnce(Return(sizeof(struct icmphdr)));
  task_environment_.FastForwardBy(IcmpSession::kEchoRequestInterval -
                                  result[1]);

  // Receive 3rd response (T + 2.5s).
  task_environment_.FastForwardBy(result[2]);
  icmp_session_->OnEchoReplyReceivedForTesting(
      ConcatBuffers(kIpHeader, kIcmpEchoReply3));
}

TEST_F(IcmpSessionTest, SessionSuccessIPv6) {
  struct icmp6_hdr icmp_header;
  memset(&icmp_header, 0, sizeof(icmp_header));
  icmp_header.icmp6_type = ICMP6_ECHO_REQUEST;
  icmp_header.icmp6_code = IcmpSession::kIcmpEchoCode;
  icmp_header.icmp6_id = kEchoId;
  icmp_header.icmp6_seq = 0;

  net_base::MockSocket* mock_socket = nullptr;
  EXPECT_CALL(*socket_factory_,
              Create(AF_INET6, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMPV6))
      .WillOnce([&mock_socket]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        mock_socket = socket.get();
        return socket;
      });

  EXPECT_CALL(*this, ResultCallback).Times(1);
  EXPECT_TRUE(
      icmp_session_->Start(kIPv6Address, kInterfaceIndex,
                           base::BindOnce(&IcmpSessionTest::ResultCallback,
                                          base::Unretained(this))));

  // Send 1st request.
  icmp_header.icmp6_seq = 0;
  EXPECT_CALL(
      *mock_socket,
      SendTo(ElementsAreArray(net_base::byte_utils::ToBytes(icmp_header)), 0,
             IsSocketAddressV6(kIPv6Address), sizeof(sockaddr_in6)))
      .WillOnce(Return(sizeof(struct icmphdr)));
  task_environment_.RunUntilIdle();

  // Receive 1st response.
  icmp_session_->OnEchoReplyReceivedForTesting(kIcmpV6EchoReply1);

  // Send 2nd request.
  icmp_header.icmp6_seq = 1;
  EXPECT_CALL(
      *mock_socket,
      SendTo(ElementsAreArray(net_base::byte_utils::ToBytes(icmp_header)), 0,
             IsSocketAddressV6(kIPv6Address), sizeof(sockaddr_in6)))
      .WillOnce(Return(sizeof(struct icmphdr)));
  task_environment_.FastForwardBy(IcmpSession::kEchoRequestInterval);

  // Receive 2nd response.
  icmp_session_->OnEchoReplyReceivedForTesting(kIcmpV6EchoReply2);

  // Send 3rd request.
  icmp_header.icmp6_seq = 2;
  EXPECT_CALL(
      *mock_socket,
      SendTo(ElementsAreArray(net_base::byte_utils::ToBytes(icmp_header)), 0,
             IsSocketAddressV6(kIPv6Address), sizeof(sockaddr_in6)))
      .WillOnce(Return(sizeof(struct icmphdr)));
  task_environment_.FastForwardBy(IcmpSession::kEchoRequestInterval);

  // Receive 3rd response.
  icmp_session_->OnEchoReplyReceivedForTesting(kIcmpV6EchoReply3);
}

TEST_F(IcmpSessionTest, StopSession) {
  EXPECT_CALL(*socket_factory_,
              Create(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP))
      .WillOnce([]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        EXPECT_CALL(*socket, SendTo).Times(0);
        return socket;
      });

  EXPECT_CALL(*this, ResultCallback).Times(0);
  EXPECT_TRUE(
      icmp_session_->Start(kIPv4Address, kInterfaceIndex,
                           base::BindOnce(&IcmpSessionTest::ResultCallback,
                                          base::Unretained(this))));

  icmp_session_->Stop();
  task_environment_.FastForwardBy(IcmpSession::kTimeout);
}

TEST_F(IcmpSessionTest, SessionTimeout) {
  // An empty result should be returned after timeout when ICMP requests are
  // always sent successfully, but no response is received.
  const std::vector<base::TimeDelta> result = {
      base::TimeDelta(), base::TimeDelta(), base::TimeDelta()};
  EXPECT_CALL(*this, ResultCallback(ElementsAreArray(result)));

  net_base::MockSocket* mock_socket = nullptr;
  EXPECT_CALL(*socket_factory_,
              Create(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP))
      .WillOnce([&mock_socket]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        mock_socket = socket.get();
        return socket;
      });

  EXPECT_TRUE(
      icmp_session_->Start(kIPv4Address, kInterfaceIndex,
                           base::BindOnce(&IcmpSessionTest::ResultCallback,
                                          base::Unretained(this))));

  EXPECT_CALL(*mock_socket, SendTo)
      .WillRepeatedly(Return(sizeof(struct icmphdr)));
  task_environment_.FastForwardBy(IcmpSession::kTimeout);
}

TEST(IcmpSessionStaticTest, ComputeIcmpChecksum) {
  EXPECT_EQ(
      *reinterpret_cast<const uint16_t*>(kIcmpEchoRequestEvenLenChecksum),
      IcmpSession::ComputeIcmpChecksum(
          *reinterpret_cast<const struct icmphdr*>(kIcmpEchoRequestEvenLen),
          sizeof(kIcmpEchoRequestEvenLen)));
  EXPECT_EQ(
      *reinterpret_cast<const uint16_t*>(kIcmpEchoRequestOddLenChecksum),
      IcmpSession::ComputeIcmpChecksum(
          *reinterpret_cast<const struct icmphdr*>(kIcmpEchoRequestOddLen),
          sizeof(kIcmpEchoRequestOddLen)));
}

TEST(IcmpSessionStaticTest, AnyRepliesReceived) {
  IcmpSession::IcmpSessionResult none_sent;
  EXPECT_FALSE(IcmpSession::AnyRepliesReceived(none_sent));

  IcmpSession::IcmpSessionResult two_sent_none_received = {
      base::TimeDelta(),
      base::TimeDelta(),
  };
  EXPECT_FALSE(IcmpSession::AnyRepliesReceived(two_sent_none_received));

  IcmpSession::IcmpSessionResult one_sent_one_received = {
      base::Seconds(10),
  };
  EXPECT_TRUE(IcmpSession::AnyRepliesReceived(one_sent_one_received));

  IcmpSession::IcmpSessionResult two_sent_one_received = {
      base::Seconds(20),
      base::TimeDelta(),
  };
  EXPECT_TRUE(IcmpSession::AnyRepliesReceived(two_sent_one_received));
}

TEST(IcmpSessionStaticTest, IsPacketLossPercentageGreaterThan) {
  // If we sent no echo requests out, we expect no replies, therefore we have
  // 0% packet loss.
  IcmpSession::IcmpSessionResult none_sent_none_received;
  EXPECT_FALSE(IcmpSession::IsPacketLossPercentageGreaterThan(
      none_sent_none_received, 0));

  // If we receive all replies, we experience 0% packet loss.
  IcmpSession::IcmpSessionResult three_sent_three_received = {
      base::Seconds(10),
      base::Seconds(10),
      base::Seconds(10),
  };
  EXPECT_FALSE(IcmpSession::IsPacketLossPercentageGreaterThan(
      three_sent_three_received, 0));

  // If we sent 3 requests and received 2 replies, we have ~33% packet loss.
  IcmpSession::IcmpSessionResult three_sent_two_received = {
      base::Seconds(10),
      base::Seconds(10),
      base::TimeDelta(),
  };
  EXPECT_FALSE(IcmpSession::IsPacketLossPercentageGreaterThan(
      three_sent_two_received, 60));
  EXPECT_FALSE(IcmpSession::IsPacketLossPercentageGreaterThan(
      three_sent_two_received, 33));
  EXPECT_TRUE(IcmpSession::IsPacketLossPercentageGreaterThan(
      three_sent_two_received, 32));
  EXPECT_TRUE(IcmpSession::IsPacketLossPercentageGreaterThan(
      three_sent_two_received, 10));
}

}  // namespace
}  // namespace shill
