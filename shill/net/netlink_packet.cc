// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/netlink_packet.h"

#include <algorithm>

#include <base/check.h>
#include <base/logging.h>

namespace shill {

NetlinkPacket::NetlinkPacket(base::span<const uint8_t> buf)
    : consumed_bytes_(0) {
  if (buf.size() < sizeof(header_)) {
    LOG(ERROR) << "Cannot retrieve header.";
    return;
  }

  memcpy(&header_, buf.data(), sizeof(header_));
  if (buf.size() < header_.nlmsg_len || header_.nlmsg_len < sizeof(header_)) {
    LOG(ERROR) << "Discarding incomplete / invalid message.";
    return;
  }

  payload_ = std::make_unique<std::vector<uint8_t>>(
      buf.begin() + sizeof(header_), buf.end());
}

NetlinkPacket::~NetlinkPacket() = default;

bool NetlinkPacket::IsValid() const {
  return payload_ != nullptr;
}

size_t NetlinkPacket::GetLength() const {
  return GetNlMsgHeader().nlmsg_len;
}

uint16_t NetlinkPacket::GetMessageType() const {
  return GetNlMsgHeader().nlmsg_type;
}

uint32_t NetlinkPacket::GetMessageSequence() const {
  return GetNlMsgHeader().nlmsg_seq;
}

size_t NetlinkPacket::GetRemainingLength() const {
  return GetPayload().size() - consumed_bytes_;
}

base::span<const uint8_t> NetlinkPacket::GetPayload() const {
  CHECK(IsValid());
  return *payload_;
}

base::span<const uint8_t> NetlinkPacket::ConsumeRemainingPayload() {
  const auto payload = GetPayload();
  if (payload.size() < NLA_ALIGN(consumed_bytes_)) {
    return {};
  }

  const auto remaining_payload = payload.subspan(NLA_ALIGN(consumed_bytes_));
  consumed_bytes_ = payload.size();
  return remaining_payload;
}

bool NetlinkPacket::ConsumeData(size_t len, void* data) {
  if (GetRemainingLength() < len) {
    LOG(ERROR) << "Not enough bytes remaining.";
    return false;
  }

  memcpy(data, payload_->data() + consumed_bytes_, len);
  consumed_bytes_ =
      std::min(payload_->size(), consumed_bytes_ + NLMSG_ALIGN(len));
  return true;
}

const nlmsghdr& NetlinkPacket::GetNlMsgHeader() const {
  CHECK(IsValid());
  return header_;
}

bool NetlinkPacket::GetGenlMsgHdr(genlmsghdr* header) const {
  if (GetPayload().size() < sizeof(*header)) {
    return false;
  }
  memcpy(header, payload_->data(), sizeof(*header));
  return true;
}

MutableNetlinkPacket::MutableNetlinkPacket(base::span<const uint8_t> buf)
    : NetlinkPacket(buf) {}

MutableNetlinkPacket::~MutableNetlinkPacket() = default;

void MutableNetlinkPacket::ResetConsumedBytes() {
  set_consumed_bytes(0);
}

nlmsghdr* MutableNetlinkPacket::GetMutableHeader() {
  CHECK(IsValid());
  return mutable_header();
}

std::vector<uint8_t>* MutableNetlinkPacket::GetMutablePayload() {
  CHECK(IsValid());
  return mutable_payload();
}

void MutableNetlinkPacket::SetMessageType(uint16_t type) {
  mutable_header()->nlmsg_type = type;
}

void MutableNetlinkPacket::SetMessageSequence(uint32_t sequence) {
  mutable_header()->nlmsg_seq = sequence;
}

}  // namespace shill.
