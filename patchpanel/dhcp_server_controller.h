// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
#define PATCHPANEL_DHCP_SERVER_CONTROLLER_H_

#include <optional>
#include <string>

#include <shill/net/ip_address.h>

namespace patchpanel {

// This class manages the one DHCP server on a certain network interface.
class DHCPServerController {
 public:
  // The configuration of the DHCP server. The instance is read-only once
  // created, so the configuration is always valid.
  class Config {
   public:
    // Creates the Config instance if the arguments are valid.
    // |host_ip| is IP of the DHCP server. Its prefix() determines the
    // subnet that the DHCP server serves.
    // |start_ip| and |end_ip| defines the DHCP IP range, which should be under
    // the same subnet as the DHCP server serves.
    static std::optional<Config> Create(const shill::IPAddress& host_ip,
                                        const shill::IPAddress& start_ip,
                                        const shill::IPAddress& end_ip);

    // Getter methods of each field.
    const std::string& host_ip() const { return host_ip_; }
    const std::string& netmask() const { return netmask_; }
    const std::string& start_ip() const { return start_ip_; }
    const std::string& end_ip() const { return end_ip_; }

   private:
    Config(const std::string& host_ip,
           const std::string& netmask,
           const std::string& start_ip,
           const std::string& end_ip);

    friend std::ostream& operator<<(std::ostream& os, const Config& config);

    const std::string host_ip_;
    const std::string netmask_;
    const std::string start_ip_;
    const std::string end_ip_;
  };

  explicit DHCPServerController(const std::string& ifname);

  DHCPServerController(const DHCPServerController&) = delete;
  DHCPServerController& operator=(const DHCPServerController&) = delete;

  virtual ~DHCPServerController();

  // Starts a DHCP server at the |ifname_| interface. Returns true if the server
  // is running successfully.
  bool Start(const Config& config);

  // Stops the DHCP server. No-op if the server is not running.
  void Stop();

 private:
  // The network interface that the DHCP server listens.
  std::string ifname_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
