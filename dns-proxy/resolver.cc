// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolver.h"

#include <utility>

#include <base/bind.h>
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

Resolver::Resolver(base::TimeDelta timeout)
    : always_on_doh_(false),
      doh_enabled_(false),
      ares_client_(new AresClient(timeout)),
      curl_client_(new DoHCurlClient(timeout)) {}

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

void Resolver::HandleAresResult(void* ctx,
                                int status,
                                uint8_t* msg,
                                size_t len) {
  std::unique_ptr<SocketFd> sock_fd(static_cast<SocketFd*>(ctx));
  if (status != ARES_SUCCESS) {
    LOG(ERROR) << "Failed to do ares lookup: " << ares_strerror(status);
    return;
  }
  ReplyDNS(sock_fd.get(), msg, len);
}

void Resolver::HandleCurlResult(void* ctx,
                                int64_t http_code,
                                uint8_t* msg,
                                size_t len) {
  SocketFd* sock_fd = static_cast<SocketFd*>(ctx);
  // TODO(jasongustaman): Handle other HTTP status code.
  if (http_code != kHTTPOk) {
    LOG(ERROR) << "Failed to do curl lookup, HTTP status code " << http_code;
    delete sock_fd;
    return;
  }
  ReplyDNS(sock_fd, msg, len);
  delete sock_fd;
}

void Resolver::ReplyDNS(SocketFd* sock_fd, uint8_t* msg, size_t len) {
  if (sendto(sock_fd->fd, msg, len, 0,
             reinterpret_cast<struct sockaddr*>(&sock_fd->src),
             sock_fd->len) <= 0) {
    PLOG(ERROR) << "sendto() " << sock_fd->fd << " failed";
  }
}

void Resolver::SetNameServers(const std::vector<std::string>& name_servers) {
  ares_client_->SetNameServers(name_servers);
  curl_client_->SetNameServers(name_servers);
}

void Resolver::SetDoHProviders(const std::vector<std::string>& doh_providers,
                               bool always_on_doh) {
  always_on_doh_ = always_on_doh;
  doh_enabled_ = !doh_providers.empty();
  curl_client_->SetDoHProviders(doh_providers);
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

  if (doh_enabled_) {
    if (curl_client_->Resolve(data, len,
                              base::BindOnce(&Resolver::HandleCurlResult,
                                             weak_factory_.GetWeakPtr()),
                              reinterpret_cast<void*>(sock_fd)) ||
        always_on_doh_) {
      return;
    }
  }
  ares_client_->Resolve(
      reinterpret_cast<const unsigned char*>(data), len,
      base::BindOnce(&Resolver::HandleAresResult, weak_factory_.GetWeakPtr()),
      reinterpret_cast<void*>(sock_fd));
}
}  // namespace dns_proxy
