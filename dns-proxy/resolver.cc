// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolver.h"

#include <utility>

#include <chromeos/patchpanel/net_util.h>

// Using directive is necessary to have the overloaded function for socket data
// structure available.
using patchpanel::operator<<;

namespace {
constexpr uint32_t kBufSize = 65536;
}  // namespace

namespace dns_proxy {

Resolver::SocketFd::SocketFd(int type, int fd) : type(type), fd(fd) {
  len = sizeof(src);
}

bool Resolver::Listen(struct sockaddr* addr) {
  // TODO(jasongustaman): Listen on TCP.
  return ListenUDP(addr);
}

bool Resolver::ListenUDP(struct sockaddr* addr) {
  udp_src_ =
      std::make_unique<patchpanel::Socket>(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK);

  // TODO(jasongustaman): Listen on IPv6.
  if (!udp_src_->Bind(addr, sizeof(*addr))) {
    LOG(ERROR) << "Cannot bind source socket to " << *addr;
    udp_src_.reset();
    return false;
  }

  // Start listening.
  LOG(INFO) << "Accepting connections on " << *addr;
  udp_src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      udp_src_->fd(),
      base::BindRepeating(&Resolver::OnDNSQuery, weak_factory_.GetWeakPtr(),
                          udp_src_->fd(), SOCK_DGRAM));
  return true;
}

void Resolver::ReplyDNS(SocketFd* sock_fd, const char* data, int len) {
  if (sendto(sock_fd->fd, data, len, 0,
             reinterpret_cast<struct sockaddr*>(&sock_fd->src),
             sock_fd->len) <= 0) {
    PLOG(ERROR) << "sendto() " << sock_fd->fd << " failed";
  }
}

void Resolver::SetNameServers(const std::vector<std::string>& name_servers) {
  // TODO(jasongustaman): Set DNS servers for CURL and Ares.
}

void Resolver::SetDoHProviders(const std::vector<std::string>& doh_providers,
                               bool always_on) {
  // TODO(jasongustaman): Set DoH servers for CURL.
  always_on_doh_ = always_on;
}

void Resolver::OnDNSQuery(int fd, int type) {
  char data[kBufSize];

  // Initialize SocketFd to carry necessary data. |sock_fd| must be freed when
  // it is done used.
  SocketFd* sock_fd = new SocketFd(type, fd);
  ssize_t len = recvfrom(fd, data, kBufSize, 0,
                         reinterpret_cast<struct sockaddr*>(&sock_fd->src),
                         &sock_fd->len);
  if (len < 0) {
    PLOG(WARNING) << "recvfrom failed";
    delete sock_fd;
    return;
  }

  // TODO(jasongustaman): Query DoH using CURL, fallback upon unexpected
  // error.
  // TODO(jasongustaman): Query DNS using Ares.
  delete sock_fd;
}
}  // namespace dns_proxy
