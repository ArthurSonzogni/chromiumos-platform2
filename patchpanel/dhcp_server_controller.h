// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
#define PATCHPANEL_DHCP_SERVER_CONTROLLER_H_

#include <optional>
#include <string>

#include <shill/net/ip_address.h>

namespace shill {
class ProcessManager;
}  // namespace shill

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

    std::string host_ip_;
    std::string netmask_;
    std::string start_ip_;
    std::string end_ip_;
  };

  explicit DHCPServerController(const std::string& ifname);

  DHCPServerController(const DHCPServerController&) = delete;
  DHCPServerController& operator=(const DHCPServerController&) = delete;

  virtual ~DHCPServerController();

  // Injects the mock ProcessManager for testing.
  void set_process_manager_for_testing(shill::ProcessManager* process_manager) {
    process_manager_ = process_manager;
  }

  // Starts a DHCP server at the |ifname_| interface. Returns true if the server
  // is created successfully. Note that if the previous server process is still
  // running, then returns false and does nothing.
  bool Start(const Config& config);

  // Stops the DHCP server. No-op if the server is not running.
  void Stop();

  // Returns true if the dnsmasq process is running.
  bool IsRunning() const;

 private:
  // The network interface that the DHCP server listens.
  const std::string ifname_;

  // The process manager to create the dnsmasq subprocess.
  shill::ProcessManager* process_manager_;

  // The pid of the dnsmasq process, nullopt iff the process is not running.
  std::optional<pid_t> pid_;
  // The configuration of the dnsmasq process, nullopt iff the process is not
  // running.
  std::optional<Config> config_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
