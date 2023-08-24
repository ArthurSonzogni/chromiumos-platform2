// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/netlink_socket.h"

#include <linux/netlink.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/byte_utils.h>
#include <net-base/mock_socket.h>

#include "shill/net/netlink_message.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::Test;

namespace shill {

class NetlinkSocketTest : public Test {
 public:
  void SetUp() override {
    auto mock_socket_factory = std::make_unique<net_base::MockSocketFactory>();
    EXPECT_CALL(*mock_socket_factory, CreateNetlink(NETLINK_GENERIC, 0, _))
        .WillOnce([&]() {
          auto socket = std::make_unique<net_base::MockSocket>();
          mock_socket_ = socket.get();
          return socket;
        });

    netlink_socket_ =
        NetlinkSocket::CreateWithSocketFactory(std::move(mock_socket_factory));
    EXPECT_NE(netlink_socket_, nullptr);
  }

 protected:
  std::unique_ptr<NetlinkSocket> netlink_socket_;
  net_base::MockSocket* mock_socket_;
};

class FakeSocketRead {
 public:
  explicit FakeSocketRead(base::span<const uint8_t> next_read_string)
      : next_read_string_(next_read_string.begin(), next_read_string.end()) {}
  // Copies |len| bytes of |next_read_string_| into |buf| and clears
  // |next_read_string_|.
  std::optional<size_t> FakeSuccessfulRead(base::span<uint8_t> buf,
                                           int flags,
                                           struct sockaddr* src_addr,
                                           socklen_t* addrlen) {
    if (buf.empty()) {
      return std::nullopt;
    }
    int read_bytes = std::min(buf.size(), next_read_string_.size());
    memcpy(buf.data(), next_read_string_.data(), read_bytes);
    next_read_string_.clear();
    return read_bytes;
  }

 private:
  std::vector<uint8_t> next_read_string_;
};

MATCHER_P(SpanEq, value, "") {
  return arg.size() == value.size() &&
         memcmp(arg.data(), value.data(), arg.size()) == 0;
}

TEST_F(NetlinkSocketTest, InitBrokenSocketTest) {
  auto mock_socket_factory = std::make_unique<net_base::MockSocketFactory>();
  EXPECT_CALL(*mock_socket_factory, CreateNetlink(NETLINK_GENERIC, 0, _))
      .WillOnce(Return(nullptr));

  auto netlink_socket =
      NetlinkSocket::CreateWithSocketFactory(std::move(mock_socket_factory));
  EXPECT_EQ(netlink_socket, nullptr);
}

TEST_F(NetlinkSocketTest, SendMessageTest) {
  const std::vector<uint8_t> message =
      net_base::byte_utils::ByteStringToBytes("This text is really arbitrary");

  // Good Send.
  EXPECT_CALL(*mock_socket_, Send(SpanEq(message), 0))
      .WillOnce(Return(message.size()));
  EXPECT_TRUE(netlink_socket_->SendMessage(message));

  // Short Send.
  EXPECT_CALL(*mock_socket_, Send(SpanEq(message), 0))
      .WillOnce(Return(message.size() - 3));
  EXPECT_FALSE(netlink_socket_->SendMessage(message));

  // Bad Send.
  EXPECT_CALL(*mock_socket_, Send(SpanEq(message), 0))
      .WillOnce(Return(std::nullopt));
  EXPECT_FALSE(netlink_socket_->SendMessage(message));
}

TEST_F(NetlinkSocketTest, SequenceNumberTest) {
  // Just a sequence number.
  const uint32_t arbitrary_number = 42;
  netlink_socket_->set_sequence_number_for_test(arbitrary_number);
  EXPECT_EQ(arbitrary_number + 1, netlink_socket_->GetSequenceNumber());

  // Make sure we don't go to |NetlinkMessage::kBroadcastSequenceNumber|.
  netlink_socket_->set_sequence_number_for_test(
      NetlinkMessage::kBroadcastSequenceNumber);
  EXPECT_NE(NetlinkMessage::kBroadcastSequenceNumber,
            netlink_socket_->GetSequenceNumber());
}

TEST_F(NetlinkSocketTest, GoodRecvMessageTest) {
  static const std::vector<uint8_t> expected_results =
      net_base::byte_utils::ByteStringToBytes(
          "Random text may include things like 'freaking fracking foo'.");

  FakeSocketRead fake_socket_read(expected_results);

  // Expect one call to get the size...
  EXPECT_CALL(*mock_socket_, RecvFrom(_, MSG_TRUNC | MSG_PEEK, _, _))
      .WillOnce(Return(expected_results.size()));

  // ...and expect a second call to get the data.
  EXPECT_CALL(*mock_socket_, RecvFrom(_, 0, _, _))
      .WillOnce(Invoke(&fake_socket_read, &FakeSocketRead::FakeSuccessfulRead));

  std::vector<uint8_t> message;
  EXPECT_TRUE(netlink_socket_->RecvMessage(&message));
  EXPECT_EQ(message, expected_results);
}

TEST_F(NetlinkSocketTest, BadRecvMessageTest) {
  std::vector<uint8_t> message;
  EXPECT_CALL(*mock_socket_, RecvFrom).WillOnce(Return(std::nullopt));
  EXPECT_FALSE(netlink_socket_->RecvMessage(&message));
}

}  // namespace shill.
