// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MOCK_SOCKET_H_
#define NET_BASE_MOCK_SOCKET_H_

#include "net-base/socket.h"

#include <memory>
#include <vector>

#include <base/files/scoped_file.h>
#include <gmock/gmock.h>

#include "net-base/export.h"

namespace net_base {

class NET_BASE_EXPORT MockSocket : public Socket {
 public:
  MockSocket();
  explicit MockSocket(base::ScopedFD fd);
  ~MockSocket() override;

  MOCK_METHOD(std::unique_ptr<Socket>,
              Accept,
              (struct sockaddr*, socklen_t*),
              (const, override));
  MOCK_METHOD(bool,
              Bind,
              (const struct sockaddr*, socklen_t),
              (const, override));
  MOCK_METHOD(bool,
              GetSockName,
              (struct sockaddr*, socklen_t*),
              (const, override));
  MOCK_METHOD(bool, Listen, (int backlog), (const, override));
  MOCK_METHOD(std::optional<int>,
              Ioctl,
              // NOLINTNEXTLINE(runtime/int)
              (unsigned long request, void* argp),
              (const, override));
  MOCK_METHOD(std::optional<size_t>,
              RecvFrom,
              (base::span<uint8_t>, int, struct sockaddr*, socklen_t*),
              (const, override));
  MOCK_METHOD(bool, RecvMessage, (std::vector<uint8_t>*), (const, override));
  MOCK_METHOD(std::optional<size_t>,
              Send,
              (base::span<const uint8_t>, int),
              (const, override));
  MOCK_METHOD(std::optional<size_t>,
              SendTo,
              (base::span<const uint8_t>,
               int,
               const struct sockaddr* dest_addr,
               socklen_t),
              (const, override));
  MOCK_METHOD(bool, SetNonBlocking, (), (const, override));
  MOCK_METHOD(bool, SetReceiveBuffer, (int size), (const, override));
};

class NET_BASE_EXPORT MockSocketFactory : public SocketFactory {
 public:
  MockSocketFactory();
  ~MockSocketFactory() override;

  MOCK_METHOD(std::unique_ptr<Socket>,
              Create,
              (int domain, int type, int protocol),
              (override));
  MOCK_METHOD(std::unique_ptr<Socket>,
              CreateNetlink,
              (int netlink_family,
               uint32_t netlink_groups_mask,
               std::optional<int> receive_buffer_size),
              (override));
};

}  // namespace net_base
#endif  // NET_BASE_MOCK_SOCKET_H_
