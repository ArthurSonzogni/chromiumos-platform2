// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/ether.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <base/bind.h>

#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/io_handler.h"
#include "shill/ip_address.h"
#include "shill/ipconfig.h"
#include "shill/logging.h"
#include "shill/rtnl_handler.h"
#include "shill/rtnl_listener.h"
#include "shill/rtnl_message.h"
#include "shill/sockets.h"

using base::Bind;
using base::Unretained;
using std::string;

namespace shill {

// Keep this large enough to avoid overflows on IPv6 SNM routing update spikes
const int RTNLHandler::kReceiveBufferSize = 512 * 1024;

namespace {
base::LazyInstance<RTNLHandler> g_rtnl_handler = LAZY_INSTANCE_INITIALIZER;
}  // namespace

RTNLHandler::RTNLHandler()
    : sockets_(NULL),
      in_request_(false),
      rtnl_socket_(-1),
      request_flags_(0),
      request_sequence_(0),
      last_dump_sequence_(0),
      rtnl_callback_(Bind(&RTNLHandler::ParseRTNL, Unretained(this))) {
  SLOG(RTNL, 2) << "RTNLHandler created";
}

RTNLHandler::~RTNLHandler() {
  SLOG(RTNL, 2) << "RTNLHandler removed";
  Stop();
}

RTNLHandler* RTNLHandler::GetInstance() {
  return g_rtnl_handler.Pointer();
}

void RTNLHandler::Start(EventDispatcher *dispatcher, Sockets *sockets) {
  struct sockaddr_nl addr;

  if (sockets_) {
    return;
  }

  rtnl_socket_ = sockets->Socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
  if (rtnl_socket_ < 0) {
    LOG(ERROR) << "Failed to open rtnl socket";
    return;
  }

  if (sockets->SetReceiveBuffer(rtnl_socket_, kReceiveBufferSize)) {
    LOG(ERROR) << "Failed to increase receive buffer size";
  }

  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE |
      RTMGRP_IPV6_IFADDR | RTMGRP_IPV6_ROUTE;

  if (sockets->Bind(rtnl_socket_,
                    reinterpret_cast<struct sockaddr *>(&addr),
                    sizeof(addr)) < 0) {
    sockets->Close(rtnl_socket_);
    rtnl_socket_ = -1;
    LOG(ERROR) << "RTNL socket bind failed";
    return;
  }

  rtnl_handler_.reset(dispatcher->CreateInputHandler(
      rtnl_socket_,
      rtnl_callback_,
      Bind(&RTNLHandler::OnReadError, Unretained(this))));
  sockets_ = sockets;

  NextRequest(last_dump_sequence_);
  SLOG(RTNL, 2) << "RTNLHandler started";
}

void RTNLHandler::Stop() {
  if (!sockets_)
    return;

  rtnl_handler_.reset();
  sockets_->Close(rtnl_socket_);
  in_request_ = false;
  sockets_ = NULL;
  request_flags_ = 0;
  SLOG(RTNL, 2) << "RTNLHandler stopped";
}

void RTNLHandler::AddListener(RTNLListener *to_add) {
  for (const auto &listener : listeners_) {
    if (to_add == listener)
      return;
  }
  listeners_.push_back(to_add);
  SLOG(RTNL, 2) << "RTNLHandler added listener";
}

void RTNLHandler::RemoveListener(RTNLListener *to_remove) {
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (to_remove == *it) {
      listeners_.erase(it);
      return;
    }
  }
  SLOG(RTNL, 2) << "RTNLHandler removed listener";
}

void RTNLHandler::SetInterfaceFlags(int interface_index, unsigned int flags,
                                    unsigned int change) {
  if (!sockets_) {
    LOG(ERROR) << __func__ << " called while not started.  "
        "Assuming we are in unit tests.";
    return;
  }

  struct rtnl_request {
    struct nlmsghdr hdr;
    struct ifinfomsg msg;
  } req;

  request_sequence_++;
  memset(&req, 0, sizeof(req));

  req.hdr.nlmsg_len = sizeof(req);
  req.hdr.nlmsg_flags = NLM_F_REQUEST;
  req.hdr.nlmsg_pid = 0;
  req.hdr.nlmsg_seq = request_sequence_;
  req.hdr.nlmsg_type = RTM_NEWLINK;
  req.msg.ifi_index = interface_index;
  req.msg.ifi_flags = flags;
  req.msg.ifi_change = change;

  if (sockets_->Send(rtnl_socket_, &req, sizeof(req), 0) < 0) {
    LOG(ERROR) << "RTNL sendto failed: " << strerror(errno);
  }
}

void RTNLHandler::RequestDump(int request_flags) {
  request_flags_ |= request_flags;

  SLOG(RTNL, 2) << "RTNLHandler got request to dump "
          << std::showbase << std::hex
          << request_flags
          << std::dec << std::noshowbase;

  if (!in_request_ && sockets_)
    NextRequest(last_dump_sequence_);
}

void RTNLHandler::DispatchEvent(int type, const RTNLMessage &msg) {
  for (const auto &listener : listeners_) {
    listener->NotifyEvent(type, msg);
  }
}

void RTNLHandler::NextRequest(uint32 seq) {
  int flag = 0;
  RTNLMessage::Type type;

  SLOG(RTNL, 2) << "RTNLHandler nextrequest " << seq << " "
                << last_dump_sequence_
                << std::showbase << std::hex
                << " " << request_flags_
                << std::dec << std::noshowbase;

  if (seq != last_dump_sequence_)
    return;

  if ((request_flags_ & kRequestAddr) != 0) {
    type = RTNLMessage::kTypeAddress;
    flag = kRequestAddr;
  } else if ((request_flags_ & kRequestRoute) != 0) {
    type = RTNLMessage::kTypeRoute;
    flag = kRequestRoute;
  } else if ((request_flags_ & kRequestLink) != 0) {
    type = RTNLMessage::kTypeLink;
    flag = kRequestLink;
  } else {
    SLOG(RTNL, 2) << "Done with requests";
    in_request_ = false;
    return;
  }

  RTNLMessage msg(
      type,
      RTNLMessage::kModeGet,
      0,
      0,
      0,
      0,
      IPAddress::kFamilyUnknown);
  CHECK(SendMessage(&msg));

  last_dump_sequence_ = msg.seq();
  request_flags_ &= ~flag;
  in_request_ = true;
}

void RTNLHandler::ParseRTNL(InputData *data) {
  unsigned char *buf = data->buf;
  unsigned char *end = buf + data->len;

  while (buf < end) {
    struct nlmsghdr *hdr = reinterpret_cast<struct nlmsghdr *>(buf);
    if (!NLMSG_OK(hdr, static_cast<unsigned int>(end - buf)))
      break;

    SLOG(RTNL, 3) << __func__ << ": received payload (" << end - buf << ")";

    RTNLMessage msg;
    ByteString payload(reinterpret_cast<unsigned char *>(hdr), hdr->nlmsg_len);
    SLOG(RTNL, 5) << "RTNL received payload length " << payload.GetLength()
                  << ": \"" << payload.HexEncode() << "\"";
    if (!msg.Decode(payload)) {
      SLOG(RTNL, 3) << __func__ << ": rtnl packet type "
                    << hdr->nlmsg_type
                    << " length " << hdr->nlmsg_len
                    << " sequence " << hdr->nlmsg_seq;

      switch (hdr->nlmsg_type) {
        case NLMSG_NOOP:
        case NLMSG_OVERRUN:
          break;
        case NLMSG_DONE:
          NextRequest(hdr->nlmsg_seq);
          break;
        case NLMSG_ERROR:
          {
            struct nlmsgerr *err =
                reinterpret_cast<nlmsgerr *>(NLMSG_DATA(hdr));
            LOG(ERROR) << "error " << -err->error << " ("
                       << strerror(-err->error) << ")";
            break;
          }
        default:
          NOTIMPLEMENTED() << "Unknown NL message type.";
      }
    } else {
      switch (msg.type()) {
        case RTNLMessage::kTypeLink:
          DispatchEvent(kRequestLink, msg);
          break;
        case RTNLMessage::kTypeAddress:
          DispatchEvent(kRequestAddr, msg);
          break;
        case RTNLMessage::kTypeRoute:
          DispatchEvent(kRequestRoute, msg);
          break;
        default:
          NOTIMPLEMENTED() << "Unknown RTNL message type.";
      }
    }
    buf += hdr->nlmsg_len;
  }
}

bool RTNLHandler::AddressRequest(int interface_index,
                                 RTNLMessage::Mode mode,
                                 int flags,
                                 const IPAddress &local,
                                 const IPAddress &broadcast,
                                 const IPAddress &peer) {
  CHECK(local.family() == broadcast.family());
  CHECK(local.family() == peer.family());

  RTNLMessage msg(
      RTNLMessage::kTypeAddress,
      mode,
      NLM_F_REQUEST | flags,
      0,
      0,
      interface_index,
      local.family());

  msg.set_address_status(RTNLMessage::AddressStatus(
      local.prefix(),
      0,
      0));

  msg.SetAttribute(IFA_LOCAL, local.address());
  if (!broadcast.IsDefault()) {
    msg.SetAttribute(IFA_BROADCAST, broadcast.address());
  }
  if (!peer.IsDefault()) {
    msg.SetAttribute(IFA_ADDRESS, peer.address());
  }

  return SendMessage(&msg);
}

bool RTNLHandler::AddInterfaceAddress(int interface_index,
                                      const IPAddress &local,
                                      const IPAddress &broadcast,
                                      const IPAddress &peer) {
    return AddressRequest(interface_index,
                          RTNLMessage::kModeAdd,
                          NLM_F_CREATE | NLM_F_EXCL | NLM_F_ECHO,
                          local,
                          broadcast,
                          peer);
}

bool RTNLHandler::RemoveInterfaceAddress(int interface_index,
                                         const IPAddress &local) {
  return AddressRequest(interface_index,
                        RTNLMessage::kModeDelete,
                        NLM_F_ECHO,
                        local,
                        IPAddress(local.family()),
                        IPAddress(local.family()));
}

bool RTNLHandler::RemoveInterface(int interface_index) {
  RTNLMessage msg(
      RTNLMessage::kTypeLink,
      RTNLMessage::kModeDelete,
      NLM_F_REQUEST,
      0,
      0,
      interface_index,
      IPAddress::kFamilyUnknown);
  return SendMessage(&msg);
}

int RTNLHandler::GetInterfaceIndex(const string &interface_name) {
  if (interface_name.empty()) {
    LOG(ERROR) << "Empty interface name -- unable to obtain index.";
    return -1;
  }
  struct ifreq ifr;
  if (interface_name.size() >= sizeof(ifr.ifr_name)) {
    LOG(ERROR) << "Interface name too long: " << interface_name.size() << " >= "
               << sizeof(ifr.ifr_name);
    return -1;
  }
  int socket = sockets_->Socket(PF_INET, SOCK_DGRAM, 0);
  if (socket < 0) {
    PLOG(ERROR) << "Unable to open INET socket";
    return -1;
  }
  ScopedSocketCloser socket_closer(sockets_, socket);
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, interface_name.c_str(), sizeof(ifr.ifr_name));
  if (sockets_->Ioctl(socket, SIOCGIFINDEX, &ifr) < 0) {
    PLOG(ERROR) << "SIOCGIFINDEX error for " << interface_name;
    return -1;
  }
  return ifr.ifr_ifindex;
}

bool RTNLHandler::SendMessage(RTNLMessage *message) {
  message->set_seq(request_sequence_);
  ByteString msgdata = message->Encode();

  if (msgdata.GetLength() == 0) {
    return false;
  }

  SLOG(RTNL, 5) << "RTNL sending payload with request sequence "
                << request_sequence_ << ", length " << msgdata.GetLength()
                << ": \"" << msgdata.HexEncode() << "\"";

  request_sequence_++;

  if (sockets_->Send(rtnl_socket_,
                     msgdata.GetConstData(),
                     msgdata.GetLength(),
                     0) < 0) {
    PLOG(ERROR) << "RTNL send failed: " << strerror(errno);
    return false;
  }

  return true;
}

void RTNLHandler::OnReadError(const Error &error) {
  LOG(FATAL) << "RTNL Socket read returns error: "
             << error.message();
}

}  // namespace shill
