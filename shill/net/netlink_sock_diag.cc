// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/netlink_sock_diag.h"

#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <sys/socket.h>

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/numerics/safe_conversions.h>

namespace {

struct SockDiagRequest {
  struct nlmsghdr header;
  struct inet_diag_req_v2 req_opts;
};

SockDiagRequest CreateDumpRequest(uint8_t family,
                                  uint8_t protocol,
                                  int sequence_number) {
  CHECK(family == AF_INET || family == AF_INET6)
      << "Unsupported SOCK_DIAG family " << family;

  SockDiagRequest request;
  request.header.nlmsg_len = sizeof(SockDiagRequest);
  request.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
  request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  request.header.nlmsg_seq = sequence_number;
  request.req_opts.sdiag_family = family;
  request.req_opts.sdiag_protocol = protocol;
  request.req_opts.idiag_states = -1;  // all states
  return request;
}

SockDiagRequest CreateDestroyRequest(uint8_t family, uint8_t protocol) {
  SockDiagRequest request;
  request.header.nlmsg_len = sizeof(SockDiagRequest);
  request.header.nlmsg_type = SOCK_DESTROY;
  request.header.nlmsg_flags = NLM_F_REQUEST;
  request.req_opts.sdiag_family = family;
  request.req_opts.sdiag_protocol = protocol;
  request.req_opts.idiag_states = -1;  // all states
  return request;
}

}  // namespace

namespace shill {

// static
std::unique_ptr<NetlinkSockDiag> NetlinkSockDiag::Create() {
  std::unique_ptr<net_base::Socket> socket =
      net_base::SocketFactory().CreateNetlink(NETLINK_SOCK_DIAG, 0);
  if (socket == nullptr) {
    return nullptr;
  }

  VLOG(2) << "Netlink sock_diag socket started";
  return base::WrapUnique(new NetlinkSockDiag(std::move(socket)));
}

NetlinkSockDiag::NetlinkSockDiag(std::unique_ptr<net_base::Socket> socket)
    : socket_(std::move(socket)), sequence_number_(0) {}

NetlinkSockDiag::~NetlinkSockDiag() = default;

bool NetlinkSockDiag::DestroySockets(uint8_t protocol,
                                     const net_base::IPAddress& saddr) {
  const uint8_t family =
      static_cast<uint8_t>(net_base::ToSAFamily(saddr.GetFamily()));

  std::vector<struct inet_diag_sockid> socks;
  if (!GetSockets(family, protocol, &socks))
    return false;

  const auto addr_bytes = saddr.ToByteString();
  SockDiagRequest request = CreateDestroyRequest(family, protocol);
  for (const auto& sockid : socks) {
    const auto sock_src = net_base::IPAddress::CreateFromBytes(
        {reinterpret_cast<const uint8_t*>(sockid.idiag_src),
         saddr.GetAddressLength()});
    if (sock_src != saddr) {
      continue;
    }
    VLOG(1) << "Destroying socket (" << family << ", " << protocol << ")";
    request.header.nlmsg_seq = ++sequence_number_;
    request.req_opts.id = sockid;
    if (socket_->Send(
            {reinterpret_cast<const uint8_t*>(&request), sizeof(request)}, 0) <
        0) {
      PLOG(ERROR) << "Failed to write request to netlink socket";
      return false;
    }
  }
  return true;
}

bool NetlinkSockDiag::GetSockets(
    uint8_t family,
    uint8_t protocol,
    std::vector<struct inet_diag_sockid>* out_socks) {
  CHECK(out_socks);
  SockDiagRequest request =
      CreateDumpRequest(family, protocol, ++sequence_number_);
  if (socket_->Send(
          {reinterpret_cast<const uint8_t*>(&request), sizeof(request)}, 0) <
      0) {
    PLOG(ERROR) << "Failed to write sock_diag request to netlink socket "
                << "(family: " << family << ", protocol: " << protocol << ")";
    return false;
  }

  return ReadDumpContents(out_socks);
}

bool NetlinkSockDiag::ReadDumpContents(
    std::vector<struct inet_diag_sockid>* out_socks) {
  uint8_t buf[8192];

  out_socks->clear();

  for (;;) {
    std::optional<size_t> bytes_read =
        socket_->RecvFrom(buf, 0, nullptr, nullptr);
    if (!bytes_read) {
      PLOG(ERROR) << "Failed to read from netlink socket";
      return false;
    }

    for (nlmsghdr* nlh = reinterpret_cast<nlmsghdr*>(buf);
         NLMSG_OK(nlh, *bytes_read); nlh = NLMSG_NEXT(nlh, *bytes_read)) {
      switch (nlh->nlmsg_type) {
        case NLMSG_DONE:
          return true;
        case NLMSG_ERROR: {
          const nlmsgerr* err =
              reinterpret_cast<const nlmsgerr*> NLMSG_DATA(nlh);
          const char* err_msg = "Error parsing sock_diag netlink socket dump";
          if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*err))) {
            LOG(ERROR) << err_msg;
          } else {
            errno = -err->error;
            PLOG(ERROR) << err_msg;
          }
          return false;
        }
        case SOCK_DIAG_BY_FAMILY:
          struct inet_diag_msg current_msg;
          memcpy(&current_msg, NLMSG_DATA(nlh), sizeof(current_msg));
          out_socks->push_back(current_msg.id);
          break;
        default:
          LOG(WARNING) << "Ignoring unexpected netlink message type "
                       << nlh->nlmsg_type;
          break;
      }
    }
  }
}

}  // namespace shill
