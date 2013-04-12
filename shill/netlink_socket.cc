// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/netlink_socket.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <sys/socket.h>

#include <iomanip>
#include <string>

#include <base/logging.h>
#include <base/stringprintf.h>

#include "shill/logging.h"
#include "shill/nl80211_message.h"
#include "shill/sockets.h"

using base::StringAppendF;
using std::string;

// This is from a version of linux/socket.h that we don't have.
#define SOL_NETLINK 270

namespace shill {

// Keep this large enough to avoid overflows on IPv6 SNM routing update spikes
const int NetlinkSocket::kReceiveBufferSize = 512 * 1024;

NetlinkSocket::NetlinkSocket() : sequence_number_(0), file_descriptor_(-1) {}

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

  // The following is stolen directly from RTNLHandler.
  // TODO(wdg): refactor this and RTNLHandler together to use common code.
  // crosbug.com/39842

  file_descriptor_ = sockets_->Socket(PF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC);
  if (file_descriptor_ < 0) {
    LOG(ERROR) << "Failed to open netlink socket";
    return false;
  }

  if (sockets_->SetReceiveBuffer(file_descriptor_, kReceiveBufferSize)) {
    LOG(ERROR) << "Failed to increase receive buffer size";
  }

  struct sockaddr_nl addr;
  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;

  if (sockets_->Bind(file_descriptor_,
                    reinterpret_cast<struct sockaddr *>(&addr),
                    sizeof(addr)) < 0) {
    sockets_->Close(file_descriptor_);
    file_descriptor_ = -1;
    LOG(ERROR) << "Netlink socket bind failed";
    return false;
  }
  SLOG(WiFi, 2) << "Netlink socket started";

  return true;
}

bool NetlinkSocket::RecvMessage(ByteString *message) {
  if (!message) {
    LOG(ERROR) << "Null |message|";
    return false;
  }

  // Determine the amount of data currently waiting.
  const size_t kDummyReadByteCount = 1;
  ByteString dummy_read(kDummyReadByteCount);
  ssize_t result;
  result = sockets_->RecvFrom(
      file_descriptor_,
      dummy_read.GetData(),
      dummy_read.GetLength(),
      MSG_TRUNC | MSG_PEEK,
      NULL,
      NULL);
  if (result < 0) {
    PLOG(ERROR) << "Socket recvfrom failed.";
    return false;
  }

  // Read the data that was waiting when we did our previous read.
  message->Resize(result);
  result = sockets_->RecvFrom(
      file_descriptor_,
      message->GetData(),
      message->GetLength(),
      0,
      NULL,
      NULL);
  if (result < 0) {
    PLOG(ERROR) << "Second socket recvfrom failed.";
    return false;
  }
  return true;
}

bool NetlinkSocket::SendMessage(const ByteString &out_msg) {
  ssize_t result = sockets_->Send(file_descriptor(), out_msg.GetConstData(),
                                  out_msg.GetLength(), 0);
  if (!result) {
    PLOG(ERROR) << "Send failed.";
    return false;
  }
  if (result != static_cast<ssize_t>(out_msg.GetLength())) {
    LOG(ERROR) << "Only sent " << result << " bytes out of "
               << out_msg.GetLength() << ".";
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
