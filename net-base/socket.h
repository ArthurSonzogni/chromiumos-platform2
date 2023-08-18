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
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool Bind(const struct sockaddr* addr, socklen_t addrlen) const;

  // Delegates to getsockname(fd_.get(), ...). Returns true if successful.
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool GetSockName(struct sockaddr* addr, socklen_t* addrlen) const;

  // Delegates to listen(fd_.get(), ...). Returns true if successful.
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool Listen(int backlog) const;

  // Delegates to ioctl(fd_.get(), ...). Returns the returned value of ioctl()
  // if successful. On failure, returns std::nullopt and the errno is set. The
  // caller should use PLOG() to print errno.
  // NOLINTNEXTLINE(runtime/int)
  virtual std::optional<int> Ioctl(unsigned long request, void* argp) const;

  // Delegates to recvfrom(fd_.get(), ...). On success, returns the length of
  // the received message in bytes. On failure, returns std::nullopt and the
  // errno is set. The caller should use PLOG() to print errno.
  virtual std::optional<size_t> RecvFrom(base::span<uint8_t> buf,
                                         int flags,
                                         struct sockaddr* src_addr,
                                         socklen_t* addrlen) const;

  // Delegates to send(fd_.get(), ...). On success, returns the number of bytes
  // sent. On failure, returns std::nullopt and the errno is set. The caller
  // should use PLOG() to print errno.
  virtual std::optional<size_t> Send(base::span<const uint8_t> buf,
                                     int flags) const;

  // Delegates to sendto(fd_.get(), ...). On success, returns the number of
  // bytes sent. On failure, returns std::nullopt and the errno is set. The
  // caller should use PLOG() to print errno.
  virtual std::optional<size_t> SendTo(base::span<const uint8_t> buf,
                                       int flags,
                                       const struct sockaddr* dest_addr,
                                       socklen_t addrlen) const;

  // Sets the socket file descriptor non-blocking. Returns true if successful.
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool SetNonBlocking() const;

  // Set the size of receiver buffer in bytes for the socket file descriptor.
  // Returns true if successful.
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool SetReceiveBuffer(int size) const;

 protected:
  explicit Socket(base::ScopedFD fd);

  // The socket file descriptor. It's always valid during the lifetime of the
  // Socket instance.
  base::ScopedFD fd_;
};

// Creates the Socket instance. It's used for injecting MockSocketFactory at
// testing to create the mock Socket instance.
class NET_BASE_EXPORT SocketFactory {
 public:
  // Keep this large enough to avoid overflows on IPv6 SNM routing update
  // spikes.
  static constexpr int kNetlinkReceiveBufferSize = 512 * 1024;

  SocketFactory() = default;
  virtual ~SocketFactory() = default;

  // Creates the socket instance. Delegates to socket(...) method.
  // On failure, returns nullptr and the errno is set. The caller should use
  // PLOG() to print errno.
  virtual std::unique_ptr<Socket> Create(int domain, int type, int protocol);

  // Creates the socket instance and binds to netlink. Sets the received buffer
  // size to |receive_buffer_size| if it is set.
  // Returns nullptr on failure.
  //
  // Note: setting the received buffer size above rmem_max requires
  // CAP_NET_ADMIN.
  virtual std::unique_ptr<Socket> CreateNetlink(
      int netlink_family,
      uint32_t netlink_groups_mask,
      std::optional<int> receive_buffer_size = kNetlinkReceiveBufferSize);
};

}  // namespace net_base
#endif  // NET_BASE_SOCKET_H_
