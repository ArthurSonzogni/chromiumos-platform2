// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_RESOLVER_H_
#define DNS_PROXY_RESOLVER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/patchpanel/socket.h>

namespace dns_proxy {

// Resolver receives wire-format DNS queries and proxies them to DNS server(s).
// This class supports standard plain-text resolving using c-ares and secure
// DNS / DNS-over-HTTPS (DoH) using CURL.
//
// The resolver supports both plain-text and DoH name resolution. By default,
// standard DNS will be performed using the name servers passed to
// SetNameServers. DNS over HTTPS will be used if secure DNS providers are
// passed to SetDoHProviders. DoH can either be "always on" or "opportunistic".
// In the case of the former, only DNS over HTTPS will be performed and failures
// are final. In the case of latter, if DNS over HTTP fails, it will fall back
// to standard plain-text DNS.
//
// Resolver listens on UDP and TCP port 53.
class Resolver {
 public:
  Resolver() = default;
  virtual ~Resolver() = default;

  // Listens for incoming requests on address |addr|.
  // Listening on default DNS port (53) requires CAP_NET_BIND_SERVICE.
  // TODO(jasongustaman): Listen on IPv6.
  virtual bool Listen(struct sockaddr* addr);

  // Set standard DNS and DNS-over-HTTPS servers endpoints.
  // If DoH servers are not empty, resolving domain will be done with DoH.
  // |always_on| flag is used to disallow fallback to standard plain-text DNS.
  virtual void SetNameServers(const std::vector<std::string>& name_servers);
  virtual void SetDoHProviders(const std::vector<std::string>& doh_providers,
                               bool always_on = false);

 private:
  // |SocketFd| stores client's socket data.
  // This is used to send reply to the client on callback called.
  struct SocketFd {
    SocketFd(int type, int fd);

    // |type| is either SOCK_STREAM or SOCK_DGRAM.
    const int type;
    const int fd;

    // Holds the source address of the client and it's address length.
    // At initialization, |len| value will be the size of |src|. Upon
    // receiving, |len| should be updated to be the size of the address
    // of |src|.
    // For TCP connections, |src| and |len| are not used.
    struct sockaddr_storage src;
    socklen_t len;
  };

  // Listen on an incoming DNS query on |addr|.
  bool ListenUDP(struct sockaddr* addr);

  // Handle DNS query from clients. |type| values will be either SOCK_DGRAM
  // or SOCK STREAM, for UDP and TCP respectively.
  void OnDNSQuery(int fd, int type);

  // Send back data taken from CURL or Ares to the client.
  void ReplyDNS(SocketFd* sock_fd, const char* data, int len);

  // Disallow DoH fallback to standard plain-text DNS.
  bool always_on_doh_;

  // Watch queries from |udp_src_|.
  std::unique_ptr<patchpanel::Socket> udp_src_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> udp_src_watcher_;

  base::WeakPtrFactory<Resolver> weak_factory_{this};
};
}  // namespace dns_proxy

#endif  // DNS_PROXY_RESOLVER_H_
