// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_SOCKET_H_
#define ARC_NETWORK_SOCKET_H_

#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>

namespace arc_networkd {

// Wrapper around various syscalls used for socket communications.
class Socket {
 public:
  Socket(int family, int type);
  explicit Socket(base::ScopedFD fd);
  virtual ~Socket() = default;

  bool Bind(const struct sockaddr* addr, socklen_t addrlen);
  bool Connect(const struct sockaddr* addr, socklen_t addrlen);
  bool Listen(int backlog) const;
  std::unique_ptr<Socket> Accept(struct sockaddr* addr = nullptr,
                                 socklen_t* addrlen = nullptr) const;

  ssize_t SendTo(const void* data,
                 size_t len,
                 const struct sockaddr* addr = nullptr,
                 socklen_t addrlen = 0);
  ssize_t RecvFrom(void* data,
                   size_t len,
                   struct sockaddr* addr = nullptr,
                   socklen_t addrlen = 0);

  int fd() const { return fd_.get(); }

 private:
  base::ScopedFD fd_;

  DISALLOW_COPY_AND_ASSIGN(Socket);
};

std::ostream& operator<<(std::ostream& stream, const Socket& socket);

}  // namespace arc_networkd

#endif  // ARC_NETWORK_SOCKET_H_
