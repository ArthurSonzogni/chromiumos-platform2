// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/conntrack_monitor.h"

#include <arpa/inet.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include <linux/netfilter/nf_conntrack_tuple_common.h>
#include <linux/netlink.h>
#include <linux/types.h>
#include <stdint.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/containers/fixed_flat_map.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/task/single_thread_task_runner.h>
#include <net-base/ip_address.h>
#include <net-base/socket.h>
#include <netinet/in.h>
#include <re2/re2.h>

namespace patchpanel {

namespace {
constexpr uint8_t kNewEventBitMask = (1 << 0);
constexpr uint8_t kUpdateEventBitMask = (1 << 1);
constexpr uint8_t kDestroyEventBitMask = (1 << 2);

// Get the message type of this netlink message.
std::optional<ConntrackMonitor::EventType> GetEventType(
    const struct nlmsghdr* nlh) {
  switch (nlh->nlmsg_type & 0xFF) {
    case IPCTNL_MSG_CT_NEW:
      if (nlh->nlmsg_flags & (NLM_F_CREATE | NLM_F_EXCL)) {
        return ConntrackMonitor::EventType::kNew;
      }
      return ConntrackMonitor::EventType::kUpdate;
    case IPCTNL_MSG_CT_DELETE:
      return ConntrackMonitor::EventType::kDestroy;
    default:
      return std::nullopt;
  }
}

bool NetlinkMessageError(const struct nlmsghdr* nlh) {
  return nlh->nlmsg_type == NLMSG_ERROR ||
         (nlh->nlmsg_type == NLMSG_DONE && nlh->nlmsg_flags & NLM_F_MULTI);
}
}  // namespace

std::unique_ptr<ConntrackMonitor> ConntrackMonitor::Create(
    base::span<const EventType> events,
    std::unique_ptr<net_base::SocketFactory> factory) {
  if (!factory) {
    LOG(DFATAL) << "Socket factory is null, create conntrack monitor failed";
    return nullptr;
  }
  auto sock = factory->Create(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
  if (!sock) {
    PLOG(ERROR) << "Unable to create conntrack monitor, open socket failed.";
    return nullptr;
  }

  struct sockaddr_nl local {};
  local.nl_family = AF_NETLINK;
  unsigned int addr_len = sizeof(local);
  if (!sock->GetSockName((struct sockaddr*)&local, &addr_len)) {
    PLOG(ERROR)
        << "Unable to create conntrack monitor, get socket name failed.";
    return nullptr;
  }

  uint8_t event_mask = 0;
  for (ConntrackMonitor::EventType event : events) {
    switch (event) {
      case ConntrackMonitor::EventType::kNew:
        event_mask = event_mask | kNewEventBitMask;
        break;
      case ConntrackMonitor::EventType::kUpdate:
        event_mask = event_mask | kUpdateEventBitMask;
        break;
      case ConntrackMonitor::EventType::kDestroy:
        event_mask = event_mask | kDestroyEventBitMask;
        break;
    }
  }

  local.nl_groups = event_mask;
  if (!sock->Bind((struct sockaddr*)&local, sizeof(local))) {
    PLOG(ERROR) << "Unable to create conntrack monitor, bind to socket failed.";
    return nullptr;
  }
  return base::WrapUnique(new ConntrackMonitor(std::move(sock)));
}

ConntrackMonitor::ConntrackMonitor(std::unique_ptr<net_base::Socket> sock)
    : sock_(std::move(sock)) {
  // Start watching file descriptor for the netlink socket and call
  // `OnSocketReadable` when socket is ready to read.
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      sock_->Get(), base::BindRepeating(&ConntrackMonitor::OnSocketReadable,
                                        weak_factory_.GetWeakPtr()));
}

void ConntrackMonitor::RegisterConntrackEventHandler(
    const ConntrackEventHandler& handler) {
  event_handlers_.emplace_back(handler);
}

void ConntrackMonitor::OnSocketReadable() {
  socklen_t addrlen = sizeof(struct sockaddr_nl);
  struct sockaddr_nl peer {};
  peer.nl_family = AF_NETLINK;

  // Receive from the netlink socket.
  auto ret =
      sock_->RecvFrom(buf_, /*flags=*/0, (struct sockaddr*)&peer, &addrlen);

  if (!ret) {
    PLOG(ERROR) << "Failed to receive buffer from socket.";
    return;
  }
  if (peer.nl_pid != 0) {
    LOG(ERROR) << "Ignoring message from pid: " << peer.nl_pid;
    return;
  }

  Process(static_cast<ssize_t>(*ret));
}

void ConntrackMonitor::Process(ssize_t len) {
  if (len < sizeof(struct nlmsghdr)) {
    LOG(ERROR) << "Invalid message received from socket, length is:" << len;
    return;
  }

  struct nlmsghdr* nlh = reinterpret_cast<struct nlmsghdr*>(buf_);

  // If netlink message is able to parse and is not done with the reply, keep
  // iterating.
  for (; NLMSG_OK(nlh, len) && nlh->nlmsg_type != NLMSG_DONE;
       nlh = NLMSG_NEXT(nlh, len)) {
    if (NetlinkMessageError(nlh)) {
      LOG(ERROR) << "Netlink message is not valid.";
      continue;
    }

    struct nf_conntrack* ct = nfct_new();
    base::ScopedClosureRunner destroy_nfct_cb(
        base::BindOnce(&nfct_destroy, ct));

    // Parse the netlink message to get socket information.
    nfct_nlmsg_parse(nlh, ct);
    auto family = nfct_get_attr_u8(ct, ATTR_ORIG_L3PROTO);
    auto proto = nfct_get_attr_u8(ct, ATTR_ORIG_L4PROTO);
    uint8_t tcp_state = TCP_CONNTRACK_NONE;
    switch (proto) {
      case IPPROTO_TCP:
        tcp_state = nfct_get_attr_u8(ct, ATTR_TCP_STATE);
        break;
      case IPPROTO_UDP:
        break;
      default:
        // Currently the monitor only supports TCP and UDP, ignore other
        // protocols.
        continue;
    }

    // Get source and destination addresses based on IP family.
    std::optional<net_base::IPAddress> src_addr, dst_addr;
    if (family == AF_INET) {
      auto saddr = reinterpret_cast<const uint8_t*>(
          nfct_get_attr(ct, ATTR_ORIG_IPV4_SRC));
      auto daddr = reinterpret_cast<const uint8_t*>(
          nfct_get_attr(ct, ATTR_ORIG_IPV4_DST));

      src_addr = net_base::IPAddress::CreateFromBytes(base::span<const uint8_t>(
          saddr, net_base::IPv4Address::kAddressLength));
      dst_addr = net_base::IPAddress::CreateFromBytes(base::span<const uint8_t>(
          daddr, net_base::IPv4Address::kAddressLength));
    } else if (family == AF_INET6) {
      auto saddr = reinterpret_cast<const uint8_t*>(
          nfct_get_attr(ct, ATTR_ORIG_IPV6_SRC));
      auto daddr = reinterpret_cast<const uint8_t*>(
          nfct_get_attr(ct, ATTR_ORIG_IPV6_DST));
      src_addr = net_base::IPAddress::CreateFromBytes(base::span<const uint8_t>(
          saddr, net_base::IPv6Address::kAddressLength));
      dst_addr = net_base::IPAddress::CreateFromBytes(base::span<const uint8_t>(
          daddr, net_base::IPv6Address::kAddressLength));
    } else {
      LOG(ERROR) << "Unknown IP family: " << family;
      continue;
    }

    if (!src_addr || !dst_addr) {
      LOG(ERROR) << "Failed to get IP addresses from netlink message.";
      continue;
    }

    uint16_t sport = nfct_get_attr_u16(ct, ATTR_ORIG_PORT_SRC);
    uint16_t dport = nfct_get_attr_u16(ct, ATTR_ORIG_PORT_DST);

    auto type = GetEventType(nlh);
    if (!type) {
      LOG(ERROR) << "Unknown conntrack event type";
    }

    const auto event = Event{.src = *src_addr,
                             .dst = *dst_addr,
                             .sport = sport,
                             .dport = dport,
                             .proto = proto,
                             .type = *type,
                             .state = tcp_state};
    for (const auto& h : event_handlers_) {
      h.Run(event);
    }
  }
}
}  // namespace patchpanel
