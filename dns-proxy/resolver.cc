// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/resolver.h"

#include <utility>

#include <base/bind.h>
#include <base/rand_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/patchpanel/net_util.h>

// Using directive is necessary to have the overloaded function for socket data
// structure available.
using patchpanel::operator<<;

namespace {
constexpr uint32_t kMaxClientTcpConn = 16;
// Retries are delayed by +/- |kRetryDelayJitterMultiplier| times to avoid
// coordinated spikes.
constexpr float kRetryDelayJitterMultiplier = 0.15;
}  // namespace

namespace dns_proxy {

Resolver::SocketFd::SocketFd(int type, int fd)
    : type(type), fd(fd), num_retries(0) {
  if (type == SOCK_STREAM) {
    socklen = 0;
    return;
  }
  socklen = sizeof(src);
}

Resolver::TCPConnection::TCPConnection(
    std::unique_ptr<patchpanel::Socket> sock,
    const base::Callback<void(int, int)>& callback)
    : sock(std::move(sock)) {
  watcher = base::FileDescriptorWatcher::WatchReadable(
      TCPConnection::sock->fd(),
      base::BindRepeating(callback, TCPConnection::sock->fd(), SOCK_STREAM));
}

Resolver::Resolver(base::TimeDelta timeout,
                   base::TimeDelta retry_delay,
                   int max_num_retries,
                   int max_concurrent_queries)
    : always_on_doh_(false),
      doh_enabled_(false),
      retry_delay_(retry_delay),
      max_num_retries_(max_num_retries),
      ares_client_(new AresClient(timeout)),
      curl_client_(new DoHCurlClient(timeout, max_concurrent_queries)) {}

bool Resolver::ListenTCP(struct sockaddr* addr) {
  auto tcp_src = std::make_unique<patchpanel::Socket>(
      addr->sa_family, SOCK_STREAM | SOCK_NONBLOCK);

  if (!tcp_src->Bind(addr, sizeof(*addr))) {
    LOG(ERROR) << "Cannot bind source socket to " << *addr;
    return false;
  }

  if (!tcp_src->Listen(kMaxClientTcpConn)) {
    LOG(ERROR) << "Cannot listen on " << *addr;
    return false;
  }

  // Run the accept loop.
  LOG(INFO) << "Accepting connections on " << *addr;
  tcp_src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      tcp_src->fd(), base::BindRepeating(&Resolver::OnTCPConnection,
                                         weak_factory_.GetWeakPtr()));
  tcp_src_ = std::move(tcp_src);

  return true;
}

bool Resolver::ListenUDP(struct sockaddr* addr) {
  auto udp_src = std::make_unique<patchpanel::Socket>(
      addr->sa_family, SOCK_DGRAM | SOCK_NONBLOCK);

  if (!udp_src->Bind(addr, sizeof(*addr))) {
    LOG(ERROR) << "Cannot bind source socket to " << *addr;
    return false;
  }

  // Start listening.
  LOG(INFO) << "Accepting connections on " << *addr;
  udp_src_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      udp_src->fd(),
      base::BindRepeating(&Resolver::OnDNSQuery, weak_factory_.GetWeakPtr(),
                          udp_src->fd(), SOCK_DGRAM));
  udp_src_ = std::move(udp_src);
  return true;
}

void Resolver::OnTCPConnection() {
  // TODO(jasongustaman): Handle IPv6.
  struct sockaddr_storage client_src = {};
  socklen_t sockaddr_len = sizeof(client_src);
  auto client_conn =
      tcp_src_->Accept((struct sockaddr*)&client_src, &sockaddr_len);
  if (!client_conn) {
    LOG(ERROR) << "Failed to accept TCP connection";
    return;
  }
  tcp_connections_.emplace(
      client_conn->fd(),
      new TCPConnection(std::move(client_conn),
                        base::BindRepeating(&Resolver::OnDNSQuery,
                                            weak_factory_.GetWeakPtr())));
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
                                const DoHCurlClient::CurlResult& res,
                                uint8_t* msg,
                                size_t len) {
  SocketFd* sock_fd = static_cast<SocketFd*>(ctx);
  if (res.curl_code != CURLE_OK) {
    LOG(ERROR) << "DoH resolution failed: "
               << curl_easy_strerror(res.curl_code);
    if (always_on_doh_) {
      // TODO(jasongustaman): Send failure reply with RCODE.
      delete sock_fd;
      return;
    }
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(), sock_fd,
                       true /* fallback */));
    return;
  }

  switch (res.http_code) {
    case kHTTPOk: {
      ReplyDNS(sock_fd, msg, len);
      delete sock_fd;
      return;
    }
    case kHTTPTooManyRequests: {
      if (sock_fd->num_retries >= max_num_retries_) {
        LOG(ERROR) << "Failed to resolve hostname, retried " << max_num_retries_
                   << " tries";
        delete sock_fd;
        return;
      }

      // Add jitter to avoid coordinated spikes of retries.
      double rand_multiplier = 1 - base::RandDouble() * 2;
      base::TimeDelta retry_delay_jitter =
          (1 + rand_multiplier * kRetryDelayJitterMultiplier) * retry_delay_;

      // Retry resolving the domain.
      base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                         sock_fd, false /* fallback */),
          retry_delay_jitter);
      sock_fd->num_retries++;
      return;
    }
    default: {
      LOG(ERROR) << "Failed to do curl lookup, HTTP status code "
                 << res.http_code;
      if (always_on_doh_) {
        // TODO(jasongustaman): Send failure reply with RCODE.
        delete sock_fd;
      } else {
        base::ThreadTaskRunnerHandle::Get()->PostTask(
            FROM_HERE,
            base::BindOnce(&Resolver::Resolve, weak_factory_.GetWeakPtr(),
                           sock_fd, true /* fallback */));
      }
      return;
    }
  }
}

void Resolver::ReplyDNS(SocketFd* sock_fd, uint8_t* msg, size_t len) {
  // For TCP, DNS messages have an additional 2-bytes header representing
  // the length of the query. Add the additional header for the reply.
  uint16_t dns_len = htons(len);
  struct iovec iov_out[2];
  iov_out[0].iov_base = &dns_len;
  iov_out[0].iov_len = 2;
  // For UDP, skip the additional header. By setting |iov_len| to 0, the
  // additional header |dns_len| will not be sent.
  if (sock_fd->type == SOCK_DGRAM) {
    iov_out[0].iov_len = 0;
  }
  iov_out[1].iov_base = static_cast<void*>(msg);
  iov_out[1].iov_len = len;
  struct msghdr hdr = {
      .msg_name = nullptr,
      .msg_namelen = 0,
      .msg_iov = iov_out,
      .msg_iovlen = 2,
      .msg_control = nullptr,
      .msg_controllen = 0,
  };
  if (sock_fd->type == SOCK_DGRAM) {
    hdr.msg_name = &sock_fd->src;
    hdr.msg_namelen = sock_fd->socklen;
  }
  if (sendmsg(sock_fd->fd, &hdr, 0) < 0) {
    PLOG(ERROR) << "sendmsg() " << sock_fd->fd << " failed";
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
  // Initialize SocketFd to carry necessary data. |sock_fd| must be freed when
  // it is done used.
  SocketFd* sock_fd = new SocketFd(type, fd);

  size_t buf_size;
  struct sockaddr* src;
  switch (type) {
    case SOCK_DGRAM:
      sock_fd->msg = sock_fd->buf;
      buf_size = kDNSBufSize;
      src = reinterpret_cast<struct sockaddr*>(&sock_fd->src);
      break;
    case SOCK_STREAM:
      // For TCP, DNS has an additional 2-bytes header representing the length
      // of the query. Move the receiving buffer, so it is 4-bytes aligned.
      sock_fd->msg = sock_fd->buf + 2;
      buf_size = kDNSBufSize - 2;
      src = nullptr;
      break;
    default:
      LOG(DFATAL) << "Unexpected socket type: " << type;
      return;
  }
  sock_fd->len =
      recvfrom(fd, sock_fd->msg, buf_size, 0, src, &sock_fd->socklen);
  if (sock_fd->len < 0) {
    PLOG(WARNING) << "recvfrom failed";
    delete sock_fd;
    return;
  }
  // Handle TCP connection closed.
  if (sock_fd->len == 0) {
    delete sock_fd;
    tcp_connections_.erase(fd);
    return;
  }

  // For TCP, DNS have an additional 2-bytes header representing the length of
  // the query. Trim the additional header to be used by CURL or Ares.
  if (type == SOCK_STREAM && sock_fd->len > 2) {
    sock_fd->msg += 2;
    sock_fd->len -= 2;
  }

  Resolve(sock_fd);
}

void Resolver::Resolve(SocketFd* sock_fd, bool fallback) {
  // TODO(jasongustaman): Handle Chrome traffic separately.
  if (doh_enabled_ && !fallback) {
    if (curl_client_->Resolve(sock_fd->msg, sock_fd->len,
                              base::BindRepeating(&Resolver::HandleCurlResult,
                                                  weak_factory_.GetWeakPtr()),
                              reinterpret_cast<void*>(sock_fd)) ||
        always_on_doh_) {
      return;
    }
  }
  ares_client_->Resolve(
      reinterpret_cast<const unsigned char*>(sock_fd->msg), sock_fd->len,
      base::BindOnce(&Resolver::HandleAresResult, weak_factory_.GetWeakPtr()),
      reinterpret_cast<void*>(sock_fd));
}
}  // namespace dns_proxy
