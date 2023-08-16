// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/socket.h"

#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <memory>
#include <optional>
#include <utility>

#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace net_base {
namespace {

std::optional<size_t> ToOptionalSizeT(ssize_t size) {
  if (size < 0) {
    return std::nullopt;
  }
  return static_cast<size_t>(size);
}

}  // namespace

// static
Socket::SocketFactory Socket::GetDefaultFactory() {
  return base::BindRepeating(&Socket::Create);
}

// static
std::unique_ptr<Socket> Socket::Create(int domain, int type, int protocol) {
  return CreateFromFd(base::ScopedFD(socket(domain, type, protocol)));
}

// static
std::unique_ptr<Socket> Socket::CreateFromFd(base::ScopedFD fd) {
  if (!fd.is_valid()) {
    return nullptr;
  }

  return std::unique_ptr<Socket>(new Socket(std::move(fd)));
}

// static
std::unique_ptr<Socket> Socket::CreateNetlink(SocketFactory socket_factory,
                                              int netlink_family,
                                              uint32_t netlink_groups_mask,
                                              bool set_receive_buffer) {
  auto socket =
      socket_factory.Run(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, netlink_family);
  if (!socket) {
    PLOG(ERROR) << "Failed to open netlink socket for family "
                << netlink_family;
    return nullptr;
  }

  if (set_receive_buffer) {
    if (!socket->SetReceiveBuffer(kNetlinkReceiveBufferSize)) {
      PLOG(WARNING) << "Failed to increase receive buffer size to "
                    << kNetlinkReceiveBufferSize << "b";
    }
  }

  struct sockaddr_nl addr;
  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_groups = netlink_groups_mask;

  if (!socket->Bind(reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
    PLOG(ERROR) << "Netlink socket bind failed for family " << netlink_family;
    return nullptr;
  }

  return socket;
}

Socket::Socket(base::ScopedFD fd) : fd_(std::move(fd)) {
  LOG_IF(FATAL, !fd_.is_valid()) << "the socket fd is invalid";
}
Socket::~Socket() = default;

int Socket::Get() const {
  return fd_.get();
}

// static
int Socket::Release(std::unique_ptr<Socket> socket) {
  if (socket == nullptr) {
    return -1;
  }

  return socket->fd_.release();
}

// Some system calls can be interrupted and return EINTR, but will succeed on
// retry.  The HANDLE_EINTR macro retries a call if it returns EINTR.  For a
// list of system calls that can return EINTR, see 'man 7 signal' under the
// heading "Interruption of System Calls and Library Functions by Signal
// Handlers".

std::unique_ptr<Socket> Socket::Accept(struct sockaddr* addr,
                                       socklen_t* addrlen) const {
  return CreateFromFd(
      base::ScopedFD(HANDLE_EINTR(accept(fd_.get(), addr, addrlen))));
}

bool Socket::Bind(const struct sockaddr* addr, socklen_t addrlen) const {
  return bind(fd_.get(), addr, addrlen) == 0;
}

bool Socket::GetSockName(struct sockaddr* addr, socklen_t* addrlen) const {
  return getsockname(fd_.get(), addr, addrlen) == 0;
}

bool Socket::Listen(int backlog) const {
  return listen(fd_.get(), backlog) == 0;
}

// NOLINTNEXTLINE(runtime/int)
std::optional<int> Socket::Ioctl(unsigned long request, void* argp) const {
  int res = HANDLE_EINTR(ioctl(fd_.get(), request, argp));
  if (res < 0) {
    return std::nullopt;
  }
  return res;
}

std::optional<size_t> Socket::RecvFrom(base::span<uint8_t> buf,
                                       int flags,
                                       struct sockaddr* src_addr,
                                       socklen_t* addrlen) const {
  ssize_t res = HANDLE_EINTR(
      recvfrom(fd_.get(), buf.data(), buf.size(), flags, src_addr, addrlen));
  return ToOptionalSizeT(res);
}

std::optional<size_t> Socket::Send(base::span<const uint8_t> buf,
                                   int flags) const {
  ssize_t res = HANDLE_EINTR(send(fd_.get(), buf.data(), buf.size(), flags));
  return ToOptionalSizeT(res);
}

std::optional<size_t> Socket::SendTo(base::span<const uint8_t> buf,
                                     int flags,
                                     const struct sockaddr* dest_addr,
                                     socklen_t addrlen) const {
  ssize_t res = HANDLE_EINTR(
      sendto(fd_.get(), buf.data(), buf.size(), flags, dest_addr, addrlen));
  return ToOptionalSizeT(res);
}

bool Socket::SetNonBlocking() const {
  return HANDLE_EINTR(fcntl(fd_.get(), F_SETFL,
                            fcntl(fd_.get(), F_GETFL) | O_NONBLOCK)) == 0;
}

bool Socket::SetReceiveBuffer(int size) const {
  // Note: kernel will set buffer to 2*size to allow for struct skbuff overhead
  return setsockopt(fd_.get(), SOL_SOCKET, SO_RCVBUFFORCE, &size,
                    sizeof(size)) == 0;
}

}  // namespace net_base
