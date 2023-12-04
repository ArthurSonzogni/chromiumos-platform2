// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/generic_netlink_message.h"

#include <string>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <net-base/byte_utils.h>

#include "shill/net/netlink_attribute.h"
#include "shill/net/netlink_packet.h"

namespace shill {

std::vector<uint8_t> GenericNetlinkMessage::EncodeHeader(
    uint32_t sequence_number) {
  // Build nlmsghdr.
  std::vector<uint8_t> result = NetlinkMessage::EncodeHeader(sequence_number);
  if (result.size() == 0) {
    LOG(ERROR) << "Couldn't encode message header.";
    return result;
  }

  // Build and append the genl message header.
  genlmsghdr genl_header;
  genl_header.cmd = command();
  genl_header.version = 1;
  genl_header.reserved = 0;

  std::vector<uint8_t> genl_header_bytes =
      net_base::byte_utils::ToBytes(genl_header);
  size_t genlmsghdr_with_pad = NLMSG_ALIGN(sizeof(genl_header));
  genl_header_bytes.resize(genlmsghdr_with_pad, 0);  // Zero-fill.

  nlmsghdr* pheader = reinterpret_cast<nlmsghdr*>(result.data());
  pheader->nlmsg_len += genlmsghdr_with_pad;
  result.insert(result.end(), genl_header_bytes.begin(),
                genl_header_bytes.end());
  return result;
}

std::vector<uint8_t> GenericNetlinkMessage::Encode(uint32_t sequence_number) {
  std::vector<uint8_t> result = EncodeHeader(sequence_number);
  if (result.size() == 0) {
    LOG(ERROR) << "Couldn't encode message header.";
    return result;
  }

  // Build and append attributes (padding is included by
  // AttributeList::Encode).
  const std::vector<uint8_t> attribute_bytes = attributes_->Encode();

  // Need to re-calculate |header| since |Append|, above, moves the data.
  nlmsghdr* pheader = reinterpret_cast<nlmsghdr*>(result.data());
  pheader->nlmsg_len += attribute_bytes.size();
  result.insert(result.end(), attribute_bytes.begin(), attribute_bytes.end());

  return result;
}

bool GenericNetlinkMessage::InitAndStripHeader(NetlinkPacket* packet) {
  if (!packet) {
    LOG(ERROR) << "NULL packet";
    return false;
  }
  if (!NetlinkMessage::InitAndStripHeader(packet)) {
    return false;
  }

  genlmsghdr gnlh;
  if (!packet->ConsumeData(sizeof(gnlh), &gnlh)) {
    return false;
  }

  if (command_ != gnlh.cmd) {
    LOG(WARNING) << "This object thinks it's a " << command_
                 << " but the message thinks it's a " << gnlh.cmd;
  }

  return true;
}

std::string GenericNetlinkMessage::ToString() const {
  return base::StringPrintf("Message %s (%d)", command_string(), command());
}

void GenericNetlinkMessage::Print(int header_log_level,
                                  int detail_log_level) const {
  VLOG(header_log_level) << ToString();
  attributes_->Print(detail_log_level, 1);
}

// Control Message

const uint16_t ControlNetlinkMessage::kMessageType = GENL_ID_CTRL;

bool ControlNetlinkMessage::InitFromPacket(NetlinkPacket* packet,
                                           bool is_broadcast) {
  if (!packet) {
    LOG(ERROR) << "Null |packet| parameter";
    return false;
  }

  if (!InitAndStripHeader(packet)) {
    return false;
  }

  return packet->ConsumeAttributes(
      base::BindRepeating(&NetlinkAttribute::NewControlAttributeFromId),
      attributes_);
}

// Specific Control types.

const uint8_t NewFamilyMessage::kCommand = CTRL_CMD_NEWFAMILY;
const char NewFamilyMessage::kCommandString[] = "CTRL_CMD_NEWFAMILY";

const uint8_t GetFamilyMessage::kCommand = CTRL_CMD_GETFAMILY;
const char GetFamilyMessage::kCommandString[] = "CTRL_CMD_GETFAMILY";

GetFamilyMessage::GetFamilyMessage()
    : ControlNetlinkMessage(kCommand, kCommandString) {
  attributes()->CreateStringAttribute(CTRL_ATTR_FAMILY_NAME,
                                      "CTRL_ATTR_FAMILY_NAME");
}

// static
std::unique_ptr<NetlinkMessage> ControlNetlinkMessage::CreateMessage(
    const NetlinkPacket& packet) {
  genlmsghdr header;
  if (!packet.GetGenlMsgHdr(&header)) {
    LOG(ERROR) << "Could not read genl header.";
    return nullptr;
  }

  switch (header.cmd) {
    case NewFamilyMessage::kCommand:
      return std::make_unique<NewFamilyMessage>();
    case GetFamilyMessage::kCommand:
      return std::make_unique<GetFamilyMessage>();
    default:
      LOG(WARNING) << "Unknown/unhandled netlink control message "
                   << header.cmd;
      return std::make_unique<UnknownControlMessage>(header.cmd);
  }
}

}  // namespace shill.
