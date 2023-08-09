// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_SOCKET_H_
#define NET_BASE_SOCKET_H_

#include <sys/socket.h>

#include <memory>
#include <optional>

#include <base/containers/span.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback.h>

#include "net-base/export.h"

namespace net_base {

// Represents a socket file descriptor, and provides the encapsulation for
// the standard POSIX and Linux socket operations.
class NET_BASE_EXPORT Socket {
 public:
  // The signature of Socket::Create() method. It's used for injecting the
  // MockSocket::Create() method at testing.
  using SocketFactory = base::RepeatingCallback<std::unique_ptr<Socket>(
      int domain, int type, int protocol)>;
  // Binds to the Socket::Create() method.
  static SocketFactory kDefaultSocketFactory;

  // Creates the socket instance. Delegates to socket(...) method. Returns
  // nullptr if failed.
  static std::unique_ptr<Socket> Create(int domain, int type, int protocol);

  // Creates the socket instance with the socket descriptor. Returns nullptr if
  // |fd| is invalid.
  static std::unique_ptr<Socket> CreateFromFd(base::ScopedFD fd);
  virtual ~Socket();

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  // Returns the raw socket file descriptor.
  int Get() const;

  // Releases and returns the socket file descriptor, allowing the socket to
  // remain open as the Socket is destroyed. After the call, |socket| will be
  // dropped, so the user cannot hold a Socket instance with invalid file
  // descriptor.
  [[nodiscard]] static int Release(std::unique_ptr<Socket> socket);

  // Delegates to accept(fd_.get(), ...). Returns the new connected socket.
  virtual std::unique_ptr<Socket> Accept(struct sockaddr* addr,
                                         socklen_t* addrlen) const;

  // Delegates to bind(fd_.get(), ...). Returns true if successful.
  virtual bool Bind(const struct sockaddr* addr, socklen_t addrlen) const;

  // Delegates to getsockname(fd_.get(), ...). Returns true if successful.
  virtual bool GetSockName(struct sockaddr* addr, socklen_t* addrlen) const;

  // Delegates to listen(fd_.get(), ...). Returns true if successful.
  virtual bool Listen(int backlog) const;

  // Delegates to ioctl(fd_.get(), ...). Returns true if successful.
  // NOLINTNEXTLINE(runtime/int)
  virtual bool Ioctl(unsigned long request, void* argp) const;

  // Delegates to recvfrom(fd_.get(), ...). On success, returns the length of
  // the received message in bytes. On error, returns std::nullopt.
  virtual std::optional<size_t> RecvFrom(base::span<uint8_t> buf,
                                         int flags,
                                         struct sockaddr* src_addr,
                                         socklen_t* addrlen) const;

  // Delegates to send(fd_.get(), ...). On success, returns the number of bytes
  // sent. On error, returns std::nullopt.
  virtual std::optional<size_t> Send(base::span<const uint8_t> buf,
                                     int flags) const;

  // Delegates to sendto(fd_.get(), ...). On success, returns the number of
  // bytes sent. On error, returns std::nullopt.
  virtual std::optional<size_t> SendTo(base::span<const uint8_t> buf,
                                       int flags,
                                       const struct sockaddr* dest_addr,
                                       socklen_t addrlen) const;

  // Sets the socket file descriptor non-blocking. Returns true if successful.
  virtual bool SetNonBlocking() const;

  // Set the size of receiver buffer in bytes for the socket file descriptor.
  // Returns true if successful.
  virtual bool SetReceiveBuffer(int size) const;

 protected:
  explicit Socket(base::ScopedFD fd);

  // The socket file descriptor. It's always valid during the lifetime of the
  // Socket instance.
  base::ScopedFD fd_;
};

}  // namespace net_base
#endif  // NET_BASE_SOCKET_H_
