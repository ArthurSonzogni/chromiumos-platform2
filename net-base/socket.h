// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_SOCKET_H_
#define NET_BASE_SOCKET_H_

#include <sys/socket.h>

#include <memory>
#include <optional>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <brillo/brillo_export.h>

namespace net_base {

// Represents a socket file descriptor, and provides the encapsulation for
// the standard POSIX and Linux socket operations.
class BRILLO_EXPORT Socket {
 public:
  // Creates the socket instance by socket(...) method. On failure, returns
  // nullptr and the errno is set. The caller should use PLOG() to print errno.
  static std::unique_ptr<Socket> Create(int domain, int type, int protocol = 0);

  // Creates the socket instance with the socket descriptor. Returns nullptr if
  // |fd| is invalid.
  static std::unique_ptr<Socket> CreateFromFd(base::ScopedFD fd);

  virtual ~Socket();

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  // Returns the raw socket file descriptor.
  int Get() const;

  // Sets/unsets the callback which will be called when the socket is ready to
  // be read.
  void SetReadableCallback(base::RepeatingClosure callback);
  void UnsetReadableCallback();

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

  // Delegates to connect(fd_.get(), ...). Returns true if successful.
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool Connect(const struct sockaddr* addr, socklen_t addrlen) const;

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
  virtual std::optional<size_t> RecvFrom(base::span<char> buf,
                                         int flags = 0,
                                         struct sockaddr* src_addr = nullptr,
                                         socklen_t* addrlen = nullptr) const;
  virtual std::optional<size_t> RecvFrom(base::span<uint8_t> buf,
                                         int flags = 0,
                                         struct sockaddr* src_addr = nullptr,
                                         socklen_t* addrlen = nullptr) const;

  // Reads data from the socket into |message| and returns true if successful.
  // The |message| parameter will be resized to hold the entirety of the read
  // message (and any data in |message| will be overwritten). If the socket
  // is stream-oriented, this will read all available data.
  virtual bool RecvMessage(std::vector<uint8_t>* message) const;

  // Delegates to send(fd_.get(), ...). On success, returns the number of bytes
  // sent. On failure, returns std::nullopt and the errno is set. The caller
  // should use PLOG() to print errno.
  virtual std::optional<size_t> Send(base::span<const char> buf,
                                     int flags = MSG_NOSIGNAL) const;
  virtual std::optional<size_t> Send(base::span<const uint8_t> buf,
                                     int flags = MSG_NOSIGNAL) const;

  // Delegates to sendto(fd_.get(), ...). On success, returns the number of
  // bytes sent. On failure, returns std::nullopt and the errno is set. The
  // caller should use PLOG() to print errno.
  virtual std::optional<size_t> SendTo(base::span<const char> buf,
                                       int flags,
                                       const struct sockaddr* dest_addr,
                                       socklen_t addrlen) const;
  virtual std::optional<size_t> SendTo(base::span<const uint8_t> buf,
                                       int flags,
                                       const struct sockaddr* dest_addr,
                                       socklen_t addrlen) const;

  // Delegates to setsockopt(fd_.get(), ...). Returns true if successful.
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool SetSockOpt(int level,
                          int optname,
                          base::span<const uint8_t> opt_bytes) const;

  // Set the size of receiver buffer in bytes for the socket file descriptor.
  // Returns true if successful.
  // On failure, the errno is set. The caller should use PLOG() to print errno.
  virtual bool SetReceiveBuffer(int size) const;

 protected:
  Socket(base::ScopedFD fd, int socket_type);

  // The socket file descriptor. It's always valid during the lifetime of the
  // Socket instance.
  base::ScopedFD fd_;

  // Type of the socket. This is fetched once by the static factory function
  // if an existing socket is passed in.
  int socket_type_;

  // The read watcher of the |fd_|. It should be destroyed before |fd_| is
  // closed, so it's declared after |fd_|.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

 private:
  // Helper to perform RecvMessage on SOCK_STREAM-type sockets.
  bool RecvStream(std::vector<uint8_t>* message) const;
};

// Creates the Socket instance. It's used for injecting MockSocketFactory at
// testing to create the mock Socket instance.
class BRILLO_EXPORT SocketFactory {
 public:
  // Keep this large enough to avoid overflows on IPv6 SNM routing update
  // spikes.
  static constexpr int kNetlinkReceiveBufferSize = 512 * 1024;

  SocketFactory() = default;
  virtual ~SocketFactory() = default;

  // Creates the socket instance by the Socket::Create() method.
  // On failure, returns nullptr and the errno is set. The caller should use
  // PLOG() to print errno.
  virtual std::unique_ptr<Socket> Create(int domain,
                                         int type,
                                         int protocol = 0);

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

BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       const Socket& socket);

}  // namespace net_base
#endif  // NET_BASE_SOCKET_H_
