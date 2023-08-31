// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This code is derived from the 'iw' source code.  The copyright and license
// of that code is as follows:
//
// Copyright (c) 2007, 2008  Johannes Berg
// Copyright (c) 2007  Andy Lutomirski
// Copyright (c) 2007  Mike Kershaw
// Copyright (c) 2008-2009  Luis R. Rodriguez
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#ifndef SHILL_NET_NETLINK_SOCKET_H_
#define SHILL_NET_NETLINK_SOCKET_H_

#include <memory>
#include <vector>

#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <net-base/socket.h>

#include "shill/net/shill_export.h"

namespace shill {

// Provides an abstraction to a netlink socket.  See
// http://www.infradead.org/~tgr/libnl/doc/core.html#core_netlink_fundamentals
// for documentation on how netlink sockets work (note that most of the rest of
// this document discusses libnl -- something not used by this code).
class SHILL_EXPORT NetlinkSocket {
 public:
  // Creates a NetlinkSocket instance, using the net_base::SocketFactory.
  static std::unique_ptr<NetlinkSocket> Create();

  // Creates a NetlinkSocket instance with injecting the customized
  // net_base::SocketFactory for testing.
  static std::unique_ptr<NetlinkSocket> CreateWithSocketFactory(
      std::unique_ptr<net_base::SocketFactory> socket_factory);

  NetlinkSocket(const NetlinkSocket&) = delete;
  NetlinkSocket& operator=(const NetlinkSocket&) = delete;

  virtual ~NetlinkSocket();

  // Returns the file descriptor used by the socket.
  virtual int file_descriptor() const { return socket_->Get(); }

  // Get the next message sequence number for this socket.
  // |GetSequenceNumber| won't return zero because that is the 'broadcast'
  // sequence number.
  virtual uint32_t GetSequenceNumber();

  // Delegates to net_base::Socket::RecvMessage().
  virtual bool RecvMessage(std::vector<uint8_t>* message);

  // Sends a message, returns true if successful.
  virtual bool SendMessage(base::span<const uint8_t> message);

  // Subscribes to netlink broadcast events.
  virtual bool SubscribeToEvents(uint32_t group_id);

  // Delegates to select() to wait for the socket ready to read with timeout.
  // Returns 1 if successful.
  // Returns 0 if timeout.
  // Returns -1 if error occurs, and errno is set. The caller should use PLOG to
  // print errno.
  virtual int WaitForRead(struct timeval* timeout) const;

  // Sets the value of |sequence_number_| for testing.
  void set_sequence_number_for_test(uint32_t sequence_number) {
    sequence_number_ = sequence_number;
  }

 protected:
  explicit NetlinkSocket(std::unique_ptr<net_base::Socket> socket);

  uint32_t sequence_number_ = 0;

 private:
  // The netlink socket. It's always valid during the lifetime of NetlinkSocket
  // instance.
  std::unique_ptr<net_base::Socket> socket_;
};

}  // namespace shill

#endif  // SHILL_NET_NETLINK_SOCKET_H_
