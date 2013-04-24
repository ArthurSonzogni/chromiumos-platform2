// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETLINK_MESSAGE_H_
#define SHILL_NETLINK_MESSAGE_H_

#include <map>
#include <string>

#include <base/bind.h>

#include <gtest/gtest.h>  // for FRIEND_TEST.

#include "shill/byte_string.h"

struct nlmsghdr;

namespace shill {

// Netlink messages are sent over netlink sockets to talk between user-space
// programs (like shill) and kernel modules (like the cfg80211 module).  Each
// kernel module that talks netlink potentially adds its own family header to
// the nlmsghdr (the top-level netlink message header) and, potentially, uses a
// different payload format.  The NetlinkMessage class represents that which
// is common between the different types of netlink message.
//
// The common portions of Netlink Messages start with a |nlmsghdr|.  Those
// messages look something like the following (the functions, macros, and
// datatypes described are provided by libnl -- see also
// http://www.infradead.org/~tgr/libnl/doc/core.html):
//
//         |<--------------nlmsg_total_size()----------->|
//         |       |<------nlmsg_datalen()-------------->|
//         |       |                                     |
//    -----+-----+-+-----------------------------------+-+----
//     ... |     | |            netlink payload        | |
//         |     | +------------+-+--------------------+ |
//         | nl  | |            | |                    | | nl
//         | msg |p| (optional) |p|                    |p| msg ...
//         | hdr |a| family     |a|   family payload   |a| hdr
//         |     |d| header     |d|                    |d|
//         |     | |            | |                    | |
//    -----+-----+-+------------+-+--------------------+-+----
//                  ^
//                  |
//                  +-- nlmsg_data()
//
// All NetlinkMessages sent to the kernel need a valid message type (which is
// found in the nlmsghdr structure) and all NetlinkMessages received from the
// kernel have a valid message type.  Some message types (NLMSG_NOOP,
// NLMSG_ERROR, and GENL_ID_CTRL, for example) are allocated statically; for
// those, the |message_type_| is assigned directly.
//
// Other message types ("nl80211", for example), are assigned by the kernel
// dynamically.  To get the message type, pass a closure to assign the
// message_type along with the sting to NetlinkManager::GetFamily:
//
//  nl80211_type = netlink_manager->GetFamily(Nl80211Message::kMessageType);
//
// Do all of this before you start to create NetlinkMessages so that
// NetlinkMessage can be instantiated with a valid |message_type_|.

class NetlinkMessage {
 public:
  static const uint32_t kBroadcastSequenceNumber;
  static const uint16_t kIllegalMessageType;

  explicit NetlinkMessage(uint16_t message_type) :
      flags_(0), message_type_(message_type),
      sequence_number_(kBroadcastSequenceNumber) {}
  virtual ~NetlinkMessage() {}

  // Returns a string of bytes representing the message (with it headers) and
  // any necessary padding.  These bytes are appropriately formatted to be
  // written to a netlink socket.
  virtual ByteString Encode(uint32_t sequence_number) = 0;

  // Initializes the |NetlinkMessage| from a complete and legal message
  // (potentially received from the kernel via a netlink socket).
  virtual bool InitFromNlmsg(const nlmsghdr *msg);

  uint16_t message_type() const { return message_type_; }
  void AddFlag(uint16_t new_flag) { flags_ |= new_flag; }
  uint16_t flags() const { return flags_; }
  uint32_t sequence_number() const { return sequence_number_; }
  // Logs the message.  Allows a different log level (presumably more
  // stringent) for the body of the message than the header.
  virtual void Print(int header_log_level, int detail_log_level) const = 0;

  // Logs the message's raw bytes (with minimal interpretation).
  static void PrintBytes(int log_level, const unsigned char *buf,
                         size_t num_bytes);

 protected:
  friend class NetlinkManagerTest;
  FRIEND_TEST(NetlinkManagerTest, NL80211_CMD_NOTIFY_CQM);

  // Returns a string of bytes representing an |nlmsghdr|, filled-in, and its
  // padding.
  virtual ByteString EncodeHeader(uint32_t sequence_number);
  // Reads the |nlmsghdr| and removes it from |input|.
  virtual bool InitAndStripHeader(ByteString *input);

  uint16_t flags_;
  uint16_t message_type_;
  uint32_t sequence_number_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetlinkMessage);
};


// The Error and Ack messages are received from the kernel and are combined,
// here, because they look so much alike (the only difference is that the
// error code is 0 for the Ack messages).  Error messages are received from
// the kernel in response to a sent message when there's a problem (such as
// a malformed message or a busy kernel module).  Ack messages are received
// from the kernel when a sent message has the NLM_F_ACK flag set, indicating
// that an Ack is requested.
class ErrorAckMessage : public NetlinkMessage {
 public:
  static const uint16_t kMessageType;

  ErrorAckMessage() : NetlinkMessage(kMessageType), error_(0) {}
  virtual bool InitFromNlmsg(const nlmsghdr *const_msg);
  virtual ByteString Encode(uint32_t sequence_number);
  virtual void Print(int header_log_level, int detail_log_level) const;
  std::string ToString() const;
  uint32_t error() const { return -error_; }

 private:
  uint32_t error_;

  DISALLOW_COPY_AND_ASSIGN(ErrorAckMessage);
};


class NoopMessage : public NetlinkMessage {
 public:
  static const uint16_t kMessageType;

  NoopMessage() : NetlinkMessage(kMessageType) {}
  virtual ByteString Encode(uint32_t sequence_number);
  virtual void Print(int header_log_level, int detail_log_level) const;
  std::string ToString() const { return "<NOOP>"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NoopMessage);
};


class DoneMessage : public NetlinkMessage {
 public:
  static const uint16_t kMessageType;

  DoneMessage() : NetlinkMessage(kMessageType) {}
  virtual ByteString Encode(uint32_t sequence_number);
  virtual void Print(int header_log_level, int detail_log_level) const;
  std::string ToString() const { return "<DONE with multipart message>"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DoneMessage);
};


class OverrunMessage : public NetlinkMessage {
 public:
  static const uint16_t kMessageType;

  OverrunMessage() : NetlinkMessage(kMessageType) {}
  virtual ByteString Encode(uint32_t sequence_number);
  virtual void Print(int header_log_level, int detail_log_level) const;
  std::string ToString() const { return "<OVERRUN - data lost>"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(OverrunMessage);
};


class UnknownMessage : public NetlinkMessage {
 public:
  UnknownMessage(uint16_t message_type, ByteString message_body) :
      NetlinkMessage(message_type), message_body_(message_body) {}
  virtual ByteString Encode(uint32_t sequence_number);
  virtual void Print(int header_log_level, int detail_log_level) const;

 private:
  ByteString message_body_;

  DISALLOW_COPY_AND_ASSIGN(UnknownMessage);
};


//
// Factory class.
//

class NetlinkMessageFactory {
 public:
  typedef base::Callback<NetlinkMessage *(const nlmsghdr *msg)> FactoryMethod;

  NetlinkMessageFactory() {}

  // Adds a message factory for a specific message_type.  Intended to be used
  // at initialization.
  bool AddFactoryMethod(uint16_t message_type, FactoryMethod factory);

  // Ownership of the message is passed to the caller and, as such, he should
  // delete it.
  NetlinkMessage *CreateMessage(const nlmsghdr *msg) const;

 private:
  std::map<uint16_t, FactoryMethod> factories_;

  DISALLOW_COPY_AND_ASSIGN(NetlinkMessageFactory);
};

}  // namespace shill

#endif  // SHILL_NETLINK_MESSAGE_H_
