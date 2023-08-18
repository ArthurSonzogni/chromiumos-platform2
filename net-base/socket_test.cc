// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/socket.h"

#include <fcntl.h>
#include <linux/netlink.h>

#include <cstdint>
#include <memory>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <gtest/gtest.h>

#include "net-base/mock_socket.h"

namespace net_base {
namespace {

constexpr int netlink_family = NETLINK_GENERIC;
constexpr uint32_t netlink_groups_mask = 0;

using testing::_;
using testing::Return;

TEST(Socket, CreateFromFd) {
  base::ScopedFD fd(open("/dev/null", O_RDONLY));
  int raw_fd = fd.get();

  auto socket = Socket::CreateFromFd(std::move(fd));
  EXPECT_EQ(socket->Get(), raw_fd);
}

TEST(Socket, CreateFromFdInvalid) {
  auto socket = Socket::CreateFromFd(base::ScopedFD());
  EXPECT_EQ(socket, nullptr);
}

TEST(Socket, Release) {
  base::ScopedFD fd(open("/dev/null", O_RDONLY));
  int raw_fd = fd.get();

  // Socket::Release() returns the raw fd, and not close the fd.
  auto socket = Socket::CreateFromFd(std::move(fd));
  EXPECT_EQ(Socket::Release(std::move(socket)), raw_fd);
  EXPECT_EQ(close(raw_fd), 0);
}

MATCHER_P(IsNetlinkAddr, groups, "") {
  const struct sockaddr_nl* socket_address =
      reinterpret_cast<const struct sockaddr_nl*>(arg);
  return socket_address->nl_family == AF_NETLINK &&
         socket_address->nl_groups == groups;
}

// Mock Create() method only to verify the behavior of CreateNetlink().
class MockSocketFactory : public SocketFactory {
 public:
  MockSocketFactory() = default;
  ~MockSocketFactory() override = default;

  MOCK_METHOD(std::unique_ptr<Socket>,
              Create,
              (int domain, int type, int protocol),
              (override));
};

TEST(SocketFactory, CreateNetlinkSuccess) {
  MockSocketFactory socket_factory;
  EXPECT_CALL(socket_factory,
              Create(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, netlink_family))
      .WillOnce([]() {
        auto socket = std::make_unique<MockSocket>();
        EXPECT_CALL(*socket,
                    SetReceiveBuffer(SocketFactory::kNetlinkReceiveBufferSize))
            .WillOnce(Return(true));
        EXPECT_CALL(*socket, Bind(IsNetlinkAddr(netlink_groups_mask),
                                  sizeof(struct sockaddr_nl)))
            .WillOnce(Return(true));
        return socket;
      });

  EXPECT_NE(socket_factory.CreateNetlink(netlink_family, netlink_groups_mask),
            nullptr);
}

TEST(SocketFactory, CreateNetlinkNotSetReceiveBuffer) {
  MockSocketFactory socket_factory;
  EXPECT_CALL(socket_factory, Create).WillOnce([]() {
    auto socket = std::make_unique<MockSocket>();
    EXPECT_CALL(*socket,
                SetReceiveBuffer(SocketFactory::kNetlinkReceiveBufferSize))
        .Times(0);
    EXPECT_CALL(*socket, Bind(IsNetlinkAddr(netlink_groups_mask),
                              sizeof(struct sockaddr_nl)))
        .WillOnce(Return(true));
    return socket;
  });

  EXPECT_NE(socket_factory.CreateNetlink(netlink_family, netlink_groups_mask,
                                         std::nullopt),
            nullptr);
}

TEST(SocketFactory, CreateNetlinkSocketFail) {
  MockSocketFactory socket_factory;
  EXPECT_CALL(socket_factory, Create).WillOnce(Return(nullptr));

  EXPECT_EQ(socket_factory.CreateNetlink(netlink_family, netlink_groups_mask),
            nullptr);
}

TEST(SocketFactory, CreateNetlinkBindFail) {
  MockSocketFactory socket_factory;
  EXPECT_CALL(socket_factory, Create).WillOnce([]() {
    auto socket = std::make_unique<MockSocket>();
    EXPECT_CALL(*socket,
                SetReceiveBuffer(SocketFactory::kNetlinkReceiveBufferSize))
        .WillOnce(Return(true));
    EXPECT_CALL(*socket, Bind).WillOnce(Return(false));
    return socket;
  });

  EXPECT_EQ(socket_factory.CreateNetlink(netlink_family, netlink_groups_mask),
            nullptr);
}

}  // namespace
}  // namespace net_base
