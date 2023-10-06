// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
#define PATCHPANEL_DHCP_SERVER_CONTROLLER_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>
#include <metrics/metrics_library.h>
#include <net-base/ipv4_address.h>
#include <shill/net/process_manager.h>

namespace patchpanel {

// This class manages the one IPv4 DHCP server on a certain network interface.
class DHCPServerController {
 public:
  using ExitCallback = shill::ProcessManager::ExitCallback;

  // The configuration of the DHCP server. The instance is read-only once
  // created, so the configuration is always valid.
  class Config {
   public:
    using DHCPOptions =
        std::vector<std::pair<uint8_t /*tag*/, std::string /*content*/>>;

    // Creates the Config instance if the arguments are valid.
    // |host_cidr| is CIDR of the DHCP server. Its prefix_length() determines
    // the subnet that the DHCP server serves.
    // |start_ip| and |end_ip| defines the DHCP IP range, which should be under
    // the same subnet as the DHCP server serves.
    // |dns_servers| is the list of DNS servers.
    // |domain_searches| is the list of the domain search.
    // |mtu| is the MTU of the downstream. std::nullopt means the default value.
    // |dhcp_options| is the list of the DHCP options.
    static std::optional<Config> Create(
        const net_base::IPv4CIDR& host_cidr,
        const net_base::IPv4Address& start_ip,
        const net_base::IPv4Address& end_ip,
        const std::vector<net_base::IPv4Address>& dns_servers,
        const std::vector<std::string>& domain_searches,
        const std::optional<int>& mtu,
        const DHCPOptions& dhcp_options);

    // Getter methods of each field.
    const std::string& host_ip() const { return host_ip_; }
    const std::string& netmask() const { return netmask_; }
    const std::string& start_ip() const { return start_ip_; }
    const std::string& end_ip() const { return end_ip_; }
    const std::string& dns_servers() const { return dns_servers_; }
    const std::string& domain_searches() const { return domain_searches_; }
    const std::string& mtu() const { return mtu_; }
    const DHCPOptions& dhcp_options() const { return dhcp_options_; }

   private:
    Config(const std::string& host_ip,
           const std::string& netmask,
           const std::string& start_ip,
           const std::string& end_ip,
           const std::string& dns_servers,
           const std::string& domain_searches,
           const std::string& mtu,
           const DHCPOptions& dhcp_options);

    friend std::ostream& operator<<(std::ostream& os, const Config& config);

    std::string host_ip_;
    std::string netmask_;
    std::string start_ip_;
    std::string end_ip_;
    // The comma-split string for the list of DNS servers.
    std::string dns_servers_;
    // The comma-split string for the list of domain search.
    std::string domain_searches_;
    // Empty if the MTU is default value.
    std::string mtu_;
    // The extra DHCP options.
    DHCPOptions dhcp_options_;
  };

  DHCPServerController(MetricsLibraryInterface* metrics,
                       const std::string& dhcp_events_metric_name,
                       const std::string& ifname);

  DHCPServerController(const DHCPServerController&) = delete;
  DHCPServerController& operator=(const DHCPServerController&) = delete;

  virtual ~DHCPServerController();

  // Injects the mock ProcessManager for testing.
  void set_process_manager_for_testing(shill::ProcessManager* process_manager) {
    process_manager_ = process_manager;
  }

  // Starts a DHCP server at the |ifname_| interface. Returns true if the server
  // is created successfully. Note that if the previous server process is still
  // running, then returns false and does nothing. |exit_callback| is called if
  // the server process is exited unexpectedly.
  bool Start(const Config& config, ExitCallback exit_callback);

  // Stops the DHCP server. No-op if the server is not running.
  void Stop();

  // Returns true if the dnsmasq process is running.
  bool IsRunning() const;

  // Gets the client's hostname, queried by the MAC address. Returns the empty
  // string if the MAC address is not found or the client doesn't advertise any
  // hostname.
  std::string GetClientHostname(const std::string& mac_addr) const;

 private:
  friend class DHCPServerControllerTest;
  FRIEND_TEST(DHCPServerControllerTest, GetClientHostname);

  // Callback when the process is exited unexpectedly.
  void OnProcessExitedUnexpectedly(int exit_status);

  // Callback when the log from dnsmasq is available.
  void OnDnsmasqLogReady();
  // Handles the log from dnsmasq.
  void HandleDnsmasqLog(std::string_view log);

  // UMA metrics client.
  MetricsLibraryInterface* metrics_;

  // UMA metric tracking DHCP success for this type of controllers.
  std::string dhcp_events_metric_name_;

  // The network interface that the DHCP server listens.
  const std::string ifname_;

  // The process manager to create the dnsmasq subprocess.
  shill::ProcessManager* process_manager_;

  // The pid of the dnsmasq process, nullopt iff the process is not running.
  std::optional<pid_t> pid_;
  // The configuration of the dnsmasq process, nullopt iff the process is not
  // running.
  std::optional<Config> config_;
  // The callback that is called when the dnsmasq process is exited
  // unexpectedly, null state iff the process is not running.
  ExitCallback exit_callback_;

  // The file descriptor of the dnsmasq process's stderr.
  base::ScopedFD log_fd_;
  // Monitors the file descriptor of the dnsmasq process's stderr.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> log_watcher_;
  // The client's host name, keyed by the MAC address.
  std::map<std::string, std::string> mac_addr_to_hostname_;
  // The set of all the client's host name, i.e. the set of
  // |mac_addr_to_hostname_|'s values.
  std::set<std::string> hostname_set;

  base::WeakPtrFactory<DHCPServerController> weak_ptr_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DHCP_SERVER_CONTROLLER_H_
