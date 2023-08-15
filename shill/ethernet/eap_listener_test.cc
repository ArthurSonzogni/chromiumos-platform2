// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ethernet/eap_listener.h"

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <gtest/gtest.h>
#include <net-base/mock_socket.h>

#include "shill/ethernet/eap_protocol.h"
#include "shill/mock_log.h"
#include "shill/net/mock_io_handler_factory.h"

using testing::_;
using testing::DoAll;
using testing::HasSubstr;
using testing::Invoke;
using testing::Return;
using testing::SaveArg;

namespace shill {

class EapListenerTest : public testing::Test {
 public:
  EapListenerTest() : listener_(kInterfaceIndex, kLinkName) {
    listener_.io_handler_factory_ = &io_handler_factory_;
  }

  ~EapListenerTest() override = default;

  void SetUp() override {
    listener_.set_request_received_callback(base::BindRepeating(
        &EapListenerTest::ReceiveCallback, base::Unretained(this)));
  }

  void TearDown() override { listener_.Stop(); }

  std::optional<size_t> SimulateRecvFrom(base::span<uint8_t> buf,
                                         int flags,
                                         struct sockaddr* src_addr,
                                         socklen_t* addrlen);

  // Set the socket factory method into |listener_|.
  // |method| is a lambda function that creates a net_base::MockSocket.
  void SetSocketFactory(
      std::function<std::unique_ptr<net_base::MockSocket>()> method);

  MOCK_METHOD(void, ReceiveCallback, ());

 protected:
  static constexpr int kInterfaceIndex = 123;
  static constexpr char kLinkName[] = "eth0";
  static constexpr uint8_t kEapPacketPayload[] = {
      eap_protocol::kIeee8021xEapolVersion2,
      eap_protocol::kIIeee8021xTypeEapPacket,
      0x00,
      0x00,  // Payload length (should be 5, but unparsed by EapListener).
      eap_protocol::kEapCodeRequest,
      0x00,  // Identifier (unparsed).
      0x00,
      0x00,  // Packet length (should be 5, but unparsed by EapListener).
      0x01   // Request type: Identity (not parsed by EapListener).
  };

  std::unique_ptr<net_base::Socket> CreateSocket() {
    return listener_.CreateSocket();
  }
  int GetInterfaceIndex() { return listener_.interface_index_; }
  size_t GetMaxEapPacketLength() { return EapListener::kMaxEapPacketLength; }
  net_base::MockSocket* GetSocket() {
    return reinterpret_cast<net_base::MockSocket*>(listener_.socket_.get());
  }
  void StartListener();
  void ReceiveRequest() { listener_.ReceiveRequest(GetSocket()->Get()); }

  MockIOHandlerFactory io_handler_factory_;
  EapListener listener_;

  // Tests can assign this in order to set the data isreturned in our
  // mock implementation of Sockets::RecvFrom().
  std::vector<uint8_t> recvfrom_reply_data_;
};

void EapListenerTest::SetSocketFactory(
    std::function<std::unique_ptr<net_base::MockSocket>()> method) {
  listener_.socket_factory_ = base::BindRepeating(
      [](std::function<std::unique_ptr<net_base::MockSocket>()> method,
         int domain, int type,
         int protocol) -> std::unique_ptr<net_base::Socket> {
        EXPECT_EQ(domain, PF_PACKET);
        EXPECT_EQ(type, SOCK_DGRAM | SOCK_CLOEXEC);
        EXPECT_EQ(protocol, htons(ETH_P_PAE));

        return method();
      },
      std::move(method));
}

std::optional<size_t> EapListenerTest::SimulateRecvFrom(
    base::span<uint8_t> buf,
    int flags,
    struct sockaddr* src_addr,
    socklen_t* addrlen) {
  // Mimic behavior of the real recvfrom -- copy no more than requested.
  size_t copy_length = std::min(recvfrom_reply_data_.size(), buf.size());
  memcpy(buf.data(), recvfrom_reply_data_.data(), copy_length);
  return copy_length;
}

MATCHER_P(IsEapLinkAddress, interface_index, "") {
  const struct sockaddr_ll* socket_address =
      reinterpret_cast<const struct sockaddr_ll*>(arg);
  return socket_address->sll_family == AF_PACKET &&
         socket_address->sll_protocol == htons(ETH_P_PAE) &&
         socket_address->sll_ifindex == interface_index;
}

void EapListenerTest::StartListener() {
  SetSocketFactory([]() {
    auto socket = std::make_unique<net_base::MockSocket>();
    EXPECT_CALL(*socket, SetNonBlocking()).WillOnce(Return(true));
    EXPECT_CALL(*socket,
                Bind(IsEapLinkAddress(kInterfaceIndex), sizeof(sockaddr_ll)))
        .WillOnce(Return(true));

    return socket;
  });

  int socket_fd;
  EXPECT_CALL(io_handler_factory_,
              CreateIOReadyHandler(_, IOHandler::kModeInput, _))
      .WillOnce(DoAll(SaveArg<0>(&socket_fd), Return(nullptr)));
  EXPECT_TRUE(listener_.Start());
  EXPECT_EQ(socket_fd, listener_.socket_->Get());
}

TEST_F(EapListenerTest, Constructor) {
  EXPECT_EQ(kInterfaceIndex, GetInterfaceIndex());
  EXPECT_EQ(8, GetMaxEapPacketLength());
  EXPECT_EQ(nullptr, GetSocket());
}

TEST_F(EapListenerTest, SocketOpenFail) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                       HasSubstr("Could not create EAP listener socket")))
      .Times(1);

  SetSocketFactory([]() { return nullptr; });
  EXPECT_EQ(CreateSocket(), nullptr);
}

TEST_F(EapListenerTest, SocketNonBlockingFail) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                       HasSubstr("Could not set socket to be non-blocking")))
      .Times(1);

  SetSocketFactory([]() {
    auto socket = std::make_unique<net_base::MockSocket>();
    EXPECT_CALL(*socket, SetNonBlocking).WillOnce(Return(false));
    return socket;
  });

  EXPECT_EQ(CreateSocket(), nullptr);
}

TEST_F(EapListenerTest, SocketBindFail) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                       HasSubstr("Could not bind socket to interface")))
      .Times(1);

  SetSocketFactory([]() {
    auto socket = std::make_unique<net_base::MockSocket>();
    EXPECT_CALL(*socket, SetNonBlocking).WillOnce(Return(true));
    EXPECT_CALL(*socket, Bind).WillOnce(Return(false));
    return socket;
  });

  EXPECT_EQ(CreateSocket(), nullptr);
}

TEST_F(EapListenerTest, StartSuccess) {
  StartListener();
}

TEST_F(EapListenerTest, StartMultipleTimes) {
  StartListener();
  StartListener();
}

TEST_F(EapListenerTest, Stop) {
  StartListener();

  listener_.Stop();
  EXPECT_EQ(nullptr, GetSocket());
}

TEST_F(EapListenerTest, ReceiveFail) {
  StartListener();

  EXPECT_CALL(*GetSocket(), RecvFrom(_, 0, _, _))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(*this, ReceiveCallback()).Times(0);

  ScopedMockLog log;
  // RecvFrom returns an error.
  EXPECT_CALL(
      log, Log(logging::LOGGING_ERROR, _, HasSubstr("Socket recvfrom failed")))
      .Times(1);
  ReceiveRequest();
}

TEST_F(EapListenerTest, ReceiveEmpty) {
  StartListener();

  EXPECT_CALL(*GetSocket(), RecvFrom(_, 0, _, _))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(*this, ReceiveCallback()).Times(0);
  ReceiveRequest();
}

TEST_F(EapListenerTest, ReceiveShort) {
  StartListener();

  recvfrom_reply_data_ = {kEapPacketPayload,
                          kEapPacketPayload + GetMaxEapPacketLength() - 1};
  EXPECT_CALL(*GetSocket(), RecvFrom(_, 0, _, _))
      .WillOnce(Invoke(this, &EapListenerTest::SimulateRecvFrom));
  EXPECT_CALL(*this, ReceiveCallback()).Times(0);
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       HasSubstr("Short EAP packet received")))
      .Times(1);
  ReceiveRequest();
}

TEST_F(EapListenerTest, ReceiveInvalid) {
  StartListener();
  // We're partially initializing this field, just making sure at least one
  // part of it is incorrect.
  uint8_t bad_payload[sizeof(kEapPacketPayload)] = {
      eap_protocol::kIeee8021xEapolVersion1 - 1};
  recvfrom_reply_data_ = {std::begin(bad_payload), std::end(bad_payload)};
  EXPECT_CALL(*GetSocket(), RecvFrom(_, 0, _, _))
      .WillOnce(Invoke(this, &EapListenerTest::SimulateRecvFrom));
  EXPECT_CALL(*this, ReceiveCallback()).Times(0);
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOGGING_INFO, _,
                       HasSubstr("Packet is not a valid EAP request")))
      .Times(1);
  ReceiveRequest();
}

TEST_F(EapListenerTest, ReceiveSuccess) {
  StartListener();
  recvfrom_reply_data_ = {std::begin(kEapPacketPayload),
                          std::end(kEapPacketPayload)};
  EXPECT_CALL(*GetSocket(), RecvFrom(_, 0, _, _))
      .WillOnce(Invoke(this, &EapListenerTest::SimulateRecvFrom));
  EXPECT_CALL(*this, ReceiveCallback()).Times(1);
  ReceiveRequest();
}

}  // namespace shill
