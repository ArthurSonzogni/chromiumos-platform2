// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETLINK_SOCK_DIAG_H_
#define NET_BASE_NETLINK_SOCK_DIAG_H_

#include <linux/inet_diag.h>
#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <optional>
#include <vector>

#include <brillo/brillo_export.h>

#include "net-base/ip_address.h"
#include "net-base/socket.h"

namespace net_base {

// NetlinkSockDiag allows for the destruction of sockets on the system.
// Destruction of both UDP and TCP sockets is supported. Note, however, that TCP
// sockets will not be immediately destroyed, but will first perform the TCP
// termination handshake.
//
// Also note that the proper functioning of this class is contingent on kernel
// support for SOCK_DESTROY.
class BRILLO_EXPORT NetlinkSockDiag {
 public:
  static std::unique_ptr<NetlinkSockDiag> Create();
  virtual ~NetlinkSockDiag();

  NetlinkSockDiag(const NetlinkSockDiag&) = delete;
  NetlinkSockDiag& operator=(const NetlinkSockDiag&) = delete;

  // Send SOCK_DESTROY for each socket matching the |protocol|, |saddr|, and
  // |uid| if given. This interrupts all blocking socket operations on those
  // sockets with ECONNABORTED so that the application can discard the socket
  // and make another connection. Note that this function is a blocking call.
  bool DestroySockets(uint8_t protocol,
                      const IPAddress& saddr,
                      std::optional<uid_t> uid = std::nullopt);

 private:
  // Hidden; use the static Create function above.
  explicit NetlinkSockDiag(std::unique_ptr<Socket> socket);

  // Get a list of sockets matching the family and protocol.
  bool GetSockets(uint8_t family,
                  uint8_t protocol,
                  std::vector<struct inet_diag_msg>* out_diag_msgs);

  // Read the socket dump from the netlink socket.
  bool ReadDumpContents(std::vector<struct inet_diag_msg>* out_diag_msgs);

  std::unique_ptr<Socket> socket_;
  uint32_t sequence_number_;
};

}  // namespace net_base

#endif  // NET_BASE_NETLINK_SOCK_DIAG_H_
