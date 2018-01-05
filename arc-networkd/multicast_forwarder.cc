// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc-networkd/multicast_forwarder.h"

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/time/time.h>

#include "arc-networkd/dns/dns_protocol.h"
#include "arc-networkd/dns/dns_response.h"

namespace {

const int kNumTempSockets = 4;
const int kBufSize = 1536;
const int kCleanupIntervalMs = 5000;
const int kCleanupTimeSeconds = 30;

}  // namespace

namespace arc_networkd {

bool MulticastForwarder::Start(const std::string& int_ifname,
                               const std::string& lan_ifname,
                               const std::string& mdns_ipaddr,
                               const std::string& mcast_addr,
                               unsigned short port,
                               bool allow_stateless) {
  int_ifname_ = int_ifname;
  lan_ifname_ = lan_ifname;
  port_ = port;
  allow_stateless_ = allow_stateless;

  if (!inet_aton(mcast_addr.c_str(), &mcast_addr_)) {
    LOG(ERROR) << "invalid multicast address " << mcast_addr;
    return false;
  }

  mdns_ip_.s_addr = INADDR_ANY;
  if (!mdns_ipaddr.empty() && !inet_aton(mdns_ipaddr.c_str(), &mdns_ip_)) {
    LOG(WARNING) << "invalid internal IP address " << mdns_ipaddr;
  }

  int_socket_.reset(new MulticastSocket());
  int_socket_->Bind(int_ifname, mcast_addr_, port, this);

  if (allow_stateless_) {
    lan_socket_.reset(new MulticastSocket());
    lan_socket_->Bind(lan_ifname, mcast_addr_, port, this);
    lan_ip_ = lan_socket_->interface_ip();
  }

  CleanupTask();
  return true;
}

// This callback is registered as part of MulticastSocket::Bind().
// All of our sockets use this function as a common callback.
void MulticastForwarder::OnFileCanReadWithoutBlocking(int fd) {
  char data[kBufSize];
  struct sockaddr_in fromaddr;

  ssize_t bytes = MulticastSocket::RecvFromFd(fd, data, kBufSize, &fromaddr);
  if (bytes < 0)
    return;

  unsigned short port = ntohs(fromaddr.sin_port);

  struct sockaddr_in dst = {0};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port_);
  dst.sin_addr = mcast_addr_;

  // Forward traffic that is part of an existing connection.
  for (auto& temp : temp_sockets_) {
    if (fd == temp->fd()) {
      int_socket_->SendTo(data, bytes, temp->int_addr);
      return;
    } else if (fd == int_socket_->fd() &&
               fromaddr.sin_port == temp->int_addr.sin_port) {
      TranslateMdnsIp(data, bytes);
      temp->SendTo(data, bytes, dst);
      return;
    }
  }

  // Forward stateless traffic.
  if (allow_stateless_ && port == port_) {
    if (fd == int_socket_->fd()) {
      TranslateMdnsIp(data, bytes);
      lan_socket_->SendTo(data, bytes, dst);
      return;
    } else if (fd == lan_socket_->fd()) {
      int_socket_->SendTo(data, bytes, dst);
      return;
    }
  }

  // New connection.
  if (fd != int_socket_->fd())
    return;

  std::unique_ptr<MulticastSocket> new_sock(new MulticastSocket());
  if (!new_sock->Bind(lan_ifname_, mcast_addr_, port, this) &&
      !new_sock->Bind(lan_ifname_, mcast_addr_, 0, this))
    return;
  memcpy(&new_sock->int_addr, &fromaddr, sizeof(new_sock->int_addr));

  new_sock->SendTo(data, bytes, dst);

  // This should ideally delete the LRU entry, but since idle entries are
  // purged by CleanupTask, the limit will only really be reached if
  // the daemon is flooded with requests.
  while (temp_sockets_.size() > kNumTempSockets)
    temp_sockets_.pop_back();
  temp_sockets_.push_front(std::move(new_sock));
}

void MulticastForwarder::TranslateMdnsIp(char* data, ssize_t bytes) {
  if (mdns_ip_.s_addr == INADDR_ANY) {
    return;
  }

  // Make sure this is a valid, successful DNS response from the Android host.
  if (bytes > net::dns_protocol::kMaxUDPSize || bytes <= 0) {
    return;
  }
  net::DnsResponse resp;
  memcpy(resp.io_buffer()->data(), data, bytes);
  if (!resp.InitParseWithoutQuery(bytes) ||
      !(resp.flags() & net::dns_protocol::kFlagResponse) ||
      resp.rcode() != net::dns_protocol::kRcodeNOERROR) {
    return;
  }

  // Check all A records for the internal IP, and replace it with |lan_ip_|
  // if it is found.
  net::DnsRecordParser parser = resp.Parser();
  while (!parser.AtEnd()) {
    const size_t ipv4_addr_len = sizeof(lan_ip_.s_addr);

    net::DnsResourceRecord record;
    if (!parser.ReadRecord(&record)) {
      break;
    }
    if (record.type == net::dns_protocol::kTypeA &&
        record.rdata.size() == ipv4_addr_len) {
      const char* rr_ip = record.rdata.data();
      if (mdns_ip_.s_addr ==
          reinterpret_cast<const struct in_addr*>(rr_ip)->s_addr) {
        // HACK: This is able to calculate the (variable) offset of the IPv4
        // address inside the resource record by assuming that the StringPiece
        // returns a pointer inside the io_buffer.  It works today, but
        // future libchrome changes might break it.
        size_t ip_offset = rr_ip - resp.io_buffer()->data();
        CHECK(ip_offset <= bytes - ipv4_addr_len);
        memcpy(&data[ip_offset], &lan_ip_.s_addr, ipv4_addr_len);
      }
    }
  }
}

void MulticastForwarder::CleanupTask() {
  time_t exp = time(NULL) - kCleanupTimeSeconds;
  for (auto it = temp_sockets_.begin(); it != temp_sockets_.end(); ) {
    if ((*it)->last_used() < exp)
      it = temp_sockets_.erase(it);
    else
      it++;
  }

  base::MessageLoopForIO::current()->task_runner()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&MulticastForwarder::CleanupTask,
                 weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kCleanupIntervalMs));
}

}  // namespace arc_networkd
