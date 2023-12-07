// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/netlink_socket.h"

#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

#include "net-base/netlink_message.h"

// This is from a version of linux/socket.h that we don't have.
#define SOL_NETLINK 270

namespace net_base {

std::unique_ptr<NetlinkSocket> NetlinkSocket::Create() {
  return CreateWithSocketFactory(std::make_unique<net_base::SocketFactory>());
}

std::unique_ptr<NetlinkSocket> NetlinkSocket::CreateWithSocketFactory(
    std::unique_ptr<net_base::SocketFactory> socket_factory) {
  std::unique_ptr<net_base::Socket> socket =
      socket_factory->CreateNetlink(NETLINK_GENERIC, 0);
  if (socket == nullptr) {
    PLOG(ERROR) << "Failed to create AF_NETLINK socket";
    return nullptr;
  }

  return std::unique_ptr<NetlinkSocket>(new NetlinkSocket(std::move(socket)));
}

NetlinkSocket::NetlinkSocket(std::unique_ptr<net_base::Socket> socket)
    : socket_(std::move(socket)) {}
NetlinkSocket::~NetlinkSocket() = default;

bool NetlinkSocket::RecvMessage(std::vector<uint8_t>* message) {
  return socket_->RecvMessage(message);
}

bool NetlinkSocket::SendMessage(base::span<const uint8_t> out_msg) {
  const std::optional<size_t> result = socket_->Send(out_msg, 0);
  if (!result) {
    PLOG(ERROR) << "Send failed.";
    return false;
  }
  if (*result != out_msg.size()) {
    LOG(ERROR) << "Only sent " << *result << " bytes out of " << out_msg.size()
               << ".";
    return false;
  }

  return true;
}

bool NetlinkSocket::SubscribeToEvents(uint32_t group_id) {
  int err = setsockopt(socket_->Get(), SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                       &group_id, sizeof(group_id));
  if (err < 0) {
    PLOG(ERROR) << "setsockopt didn't work.";
    return false;
  }
  return true;
}

int NetlinkSocket::WaitForRead(base::TimeDelta timeout) const {
  fd_set read_fds;
  FD_ZERO(&read_fds);
  if (socket_->Get() >= FD_SETSIZE) {
    LOG(ERROR) << "Invalid file_descriptor: " << socket_->Get();
    return -1;
  }
  FD_SET(socket_->Get(), &read_fds);

  DCHECK(!timeout.is_negative());
  struct timeval time = {
      .tv_sec = static_cast<time_t>(timeout.InSeconds()),
      .tv_usec = static_cast<suseconds_t>(
          (timeout - base::Seconds(timeout.InSeconds())).InMicroseconds()),
  };
  return HANDLE_EINTR(
      select(socket_->Get() + 1, &read_fds, nullptr, nullptr, &time));
}

uint32_t NetlinkSocket::GetSequenceNumber() {
  if (++sequence_number_ == NetlinkMessage::kBroadcastSequenceNumber) {
    ++sequence_number_;
  }
  return sequence_number_;
}

}  // namespace net_base.
