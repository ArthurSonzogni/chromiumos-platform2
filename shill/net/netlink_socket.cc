// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/netlink_socket.h"

#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include <base/logging.h>

#include "shill/net/netlink_fd.h"
#include "shill/net/netlink_message.h"
#include "shill/net/sockets.h"

// This is from a version of linux/socket.h that we don't have.
#define SOL_NETLINK 270

namespace shill {

NetlinkSocket::NetlinkSocket()
    : sequence_number_(0), file_descriptor_(Sockets::kInvalidFileDescriptor) {}

NetlinkSocket::~NetlinkSocket() {
  if (sockets_ && (file_descriptor_ >= 0)) {
    sockets_->Close(file_descriptor_);
  }
}

bool NetlinkSocket::Init() {
  // Allows for a test to set |sockets_| before calling |Init|.
  if (sockets_) {
    LOG(INFO) << "|sockets_| already has a value -- this must be a test.";
  } else {
    sockets_.reset(new Sockets);
  }

  file_descriptor_ = OpenNetlinkSocketFD(sockets_.get(), NETLINK_GENERIC, 0);
  if (file_descriptor_ == Sockets::kInvalidFileDescriptor)
    return false;

  VLOG(2) << "Netlink socket started";
  return true;
}

bool NetlinkSocket::RecvMessage(std::vector<uint8_t>* message) {
  if (!message) {
    LOG(ERROR) << "Null |message|";
    return false;
  }

  // Determine the amount of data currently waiting.
  const size_t kFakeReadByteCount = 1;
  std::vector<uint8_t> fake_read(kFakeReadByteCount);
  ssize_t result;
  result =
      sockets_->RecvFrom(file_descriptor_, fake_read.data(), fake_read.size(),
                         MSG_TRUNC | MSG_PEEK, nullptr, nullptr);
  if (result < 0) {
    PLOG(ERROR) << "Socket recvfrom failed.";
    return false;
  }

  // Read the data that was waiting when we did our previous read.
  message->resize(result, 0);
  result = sockets_->RecvFrom(file_descriptor_, message->data(),
                              message->size(), 0, nullptr, nullptr);
  if (result < 0) {
    PLOG(ERROR) << "Second socket recvfrom failed.";
    return false;
  }
  return true;
}

bool NetlinkSocket::SendMessage(base::span<const uint8_t> out_msg) {
  ssize_t result =
      sockets_->Send(file_descriptor(), out_msg.data(), out_msg.size(), 0);
  if (!result) {
    PLOG(ERROR) << "Send failed.";
    return false;
  }
  if (result != static_cast<ssize_t>(out_msg.size())) {
    LOG(ERROR) << "Only sent " << result << " bytes out of " << out_msg.size()
               << ".";
    return false;
  }

  return true;
}

bool NetlinkSocket::SubscribeToEvents(uint32_t group_id) {
  int err = setsockopt(file_descriptor_, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                       &group_id, sizeof(group_id));
  if (err < 0) {
    PLOG(ERROR) << "setsockopt didn't work.";
    return false;
  }
  return true;
}

uint32_t NetlinkSocket::GetSequenceNumber() {
  if (++sequence_number_ == NetlinkMessage::kBroadcastSequenceNumber)
    ++sequence_number_;
  return sequence_number_;
}

}  // namespace shill.
