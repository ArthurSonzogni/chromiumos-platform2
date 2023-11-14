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

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/containers/fixed_flat_map.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/lazy_instance.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/task/single_thread_task_runner.h>
#include <net-base/ip_address.h>
#include <net-base/socket.h>
#include <netinet/in.h>
#include <re2/re2.h>

namespace patchpanel {

namespace {
base::LazyInstance<ConntrackMonitor>::DestructorAtExit g_conntrack_monitor =
    LAZY_INSTANCE_INITIALIZER;

constexpr uint8_t kDefaultEventBitMask = 0;
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

uint8_t EventTypeToMask(ConntrackMonitor::EventType event) {
  switch (event) {
    case ConntrackMonitor::EventType::kNew:
      return kNewEventBitMask;
      break;
    case ConntrackMonitor::EventType::kUpdate:
      return kUpdateEventBitMask;
      break;
    case ConntrackMonitor::EventType::kDestroy:
      return kDestroyEventBitMask;
      break;
  }
  LOG(ERROR) << "Unknown event type: " << static_cast<int>(event);
  return kDefaultEventBitMask;
}
}  // namespace

ConntrackMonitor::ConntrackMonitor() = default;

void ConntrackMonitor::Start(
    base::span<const ConntrackMonitor::EventType> events) {
  // If monitor has already started, skip.
  if (sock_ != nullptr) {
    return;
  }
  sock_ = socket_factory_->Create(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER);
  if (!sock_) {
    PLOG(ERROR) << "Unable to create conntrack monitor, open socket failed.";
    return;
  }

  struct sockaddr_nl local {};
  local.nl_family = AF_NETLINK;
  unsigned int addr_len = sizeof(local);
  if (!sock_->GetSockName((struct sockaddr*)&local, &addr_len)) {
    PLOG(ERROR)
        << "Unable to create conntrack monitor, get socket name failed.";
    sock_.reset();
    return;
  }

  event_mask_ = 0;
  for (EventType event : events) {
    event_mask_ |= EventTypeToMask(event);
  }

  local.nl_groups = event_mask_;
  if (!sock_->Bind((struct sockaddr*)&local, sizeof(local))) {
    PLOG(ERROR) << "Unable to create conntrack monitor, bind to socket failed.";
    sock_.reset();
    return;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      sock_->Get(), base::BindRepeating(&ConntrackMonitor::OnSocketReadable,
                                        weak_factory_.GetWeakPtr()));

  if (!watcher_) {
    LOG(ERROR) << "Failed on watching netlink socket.";
    sock_.reset();
    return;
  }
  LOG(INFO) << "ConntrackMonitor started";
}

ConntrackMonitor::~ConntrackMonitor() {
  LOG(INFO) << "Conntrack monitor removed";
}

void ConntrackMonitor::StopForTesting() {
  watcher_.reset();
  sock_.reset();
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
  // If no handler is registered for conntrack event, skip processing.
  if (listeners_.empty()) {
    return;
  }
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
      continue;
    }

    const auto event = Event{.src = *src_addr,
                             .dst = *dst_addr,
                             .sport = sport,
                             .dport = dport,
                             .proto = proto,
                             .type = *type,
                             .state = tcp_state};
    DispatchEvent(event);
  }
}

ConntrackMonitor* ConntrackMonitor::GetInstance() {
  return g_conntrack_monitor.Pointer();
}

std::unique_ptr<ConntrackMonitor::Listener> ConntrackMonitor::AddListener(
    base::span<const ConntrackMonitor::EventType> events,
    const ConntrackMonitor::ConntrackEventHandler& callback) {
  uint8_t event_mask = 0;
  for (EventType event : events) {
    event_mask |= EventTypeToMask(event);
  }

  uint8_t listen_event = event_mask & event_mask_;
  if (listen_event == kDefaultEventBitMask) {
    LOG(ERROR) << "None of event specified by event list is supported by "
                  "monitor, creating monitor failed";
    return nullptr;
  }
  auto to_add = base::WrapUnique(
      new Listener(listen_event, callback, ConntrackMonitor::GetInstance()));
  listeners_.AddObserver(to_add.get());
  LOG(INFO) << "ConntrackMonitor added listener";
  return to_add;
}

void ConntrackMonitor::DispatchEvent(const Event& msg) {
  for (Listener& listener : listeners_) {
    listener.NotifyEvent(msg);
  }
}

ConntrackMonitor::Listener::Listener(
    uint8_t listen_flags,
    const ConntrackMonitor::ConntrackEventHandler& callback,
    ConntrackMonitor* monitor)
    : callback_(callback), monitor_(monitor) {
  listen_flags_ = listen_flags;
}

ConntrackMonitor::Listener::~Listener() {
  monitor_->listeners_.RemoveObserver(this);
  LOG(INFO) << "ConntrackMonitor removed listener";
}

void ConntrackMonitor::Listener::NotifyEvent(const Event& msg) const {
  uint8_t type = EventTypeToMask(msg.type);
  if (type & listen_flags_) {
    callback_.Run(msg);
  }
}

bool operator==(const ConntrackMonitor::Event&,
                const ConntrackMonitor::Event&) = default;
}  // namespace patchpanel
