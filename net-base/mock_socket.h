// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MOCK_SOCKET_H_
#define NET_BASE_MOCK_SOCKET_H_

#include "net-base/socket.h"

#include <memory>
#include <vector>

#include <base/files/scoped_file.h>
#include <brillo/brillo_export.h>
#include <gmock/gmock.h>

namespace net_base {

class BRILLO_EXPORT MockSocket : public Socket {
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
  MOCK_METHOD(bool, Listen, (int), (const, override));
  MOCK_METHOD(std::optional<int>,
              Ioctl,
              // NOLINTNEXTLINE(runtime/int)
              (unsigned long, void*),
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
  MOCK_METHOD(
      std::optional<size_t>,
      SendTo,
      (base::span<const uint8_t>, int, const struct sockaddr*, socklen_t),
      (const, override));
  MOCK_METHOD(bool, SetReceiveBuffer, (int), (const, override));
  MOCK_METHOD(bool,
              SetSockOpt,
              (int, int, base::span<const uint8_t>),
              (const, override));
};

class BRILLO_EXPORT MockSocketFactory : public SocketFactory {
 public:
  MockSocketFactory();
  ~MockSocketFactory() override;

  MOCK_METHOD(std::unique_ptr<Socket>, Create, (int, int, int), (override));
  MOCK_METHOD(std::unique_ptr<Socket>,
              CreateNetlink,
              (int, uint32_t, std::optional<int>),
              (override));
};

}  // namespace net_base
#endif  // NET_BASE_MOCK_SOCKET_H_
