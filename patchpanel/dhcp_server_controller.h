// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
#define PATCHPANEL_DHCP_SERVER_CONTROLLER_H_

#include <string>

namespace patchpanel {

// This class manages the one DHCP server on a certain network interface.
class DHCPServerController {
 public:
  explicit DHCPServerController(const std::string& ifname);

  DHCPServerController(const DHCPServerController&) = delete;
  DHCPServerController& operator=(const DHCPServerController&) = delete;

  virtual ~DHCPServerController();

  // Starts a DHCP server at the |ifname_| interface. Returns true if the server
  // is running successfully.
  bool Start();

  // Stops the DHCP server. No-op if the server is not running.
  void Stop();

 private:
  // The network interface that the DHCP server listens.
  std::string ifname_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
