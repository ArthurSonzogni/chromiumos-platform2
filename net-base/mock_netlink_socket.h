// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MOCK_NETLINK_SOCKET_H_
#define NET_BASE_MOCK_NETLINK_SOCKET_H_

#include "net-base/netlink_socket.h"

#include <vector>

#include <base/containers/span.h>
#include <base/time/time.h>
#include <gmock/gmock.h>

namespace net_base {

class MockNetlinkSocket : public NetlinkSocket {
 public:
  MockNetlinkSocket();
  MockNetlinkSocket(const MockNetlinkSocket&) = delete;
  MockNetlinkSocket& operator=(const MockNetlinkSocket&) = delete;

  ~MockNetlinkSocket() override;

  uint32_t GetLastSequenceNumber() const { return sequence_number_; }

  MOCK_METHOD(int, file_descriptor, (), (const, override));
  MOCK_METHOD(bool, SendMessage, (base::span<const uint8_t>), (override));
  MOCK_METHOD(bool, SubscribeToEvents, (uint32_t), (override));
  MOCK_METHOD(int, WaitForRead, (base::TimeDelta), (const, override));
  MOCK_METHOD(bool, RecvMessage, (std::vector<uint8_t>*), (override));
};

}  // namespace net_base

#endif  // NET_BASE_MOCK_NETLINK_SOCKET_H_
