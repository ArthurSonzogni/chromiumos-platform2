// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CHROMEOS_DAEMON_H_
#define SHILL_CHROMEOS_DAEMON_H_

#include <memory>
#include <string>
#include <vector>

#include "shill/event_dispatcher.h"
#include "shill/glib.h"

#if !defined(DISABLE_WIFI)
#include "shill/wifi/callback80211_metrics.h"
#endif  // DISABLE_WIFI

namespace shill {

class Config;
class ControlInterface;
class DHCPProvider;
class Error;
class Manager;
class Metrics;
class RoutingTable;
class RTNLHandler;

#if !defined(DISABLE_WIFI)
class NetlinkManager;
#endif  // DISABLE_WIFI

class ChromeosDaemon {
 public:
  // Run-time settings retrieved from command line.
  struct Settings {
    Settings()
        : ignore_unknown_ethernet(false),
          minimum_mtu(0),
          passive_mode(false),
          use_portal_list(false) {}
    std::string accept_hostname_from;
    std::string default_technology_order;
    std::vector<std::string> device_blacklist;
    std::vector<std::string> dhcpv6_enabled_devices;
    bool ignore_unknown_ethernet;
    int minimum_mtu;
    bool passive_mode;
    std::string portal_list;
    std::string prepend_dns_servers;
    bool use_portal_list;
  };

  ChromeosDaemon(const Settings& settings,
                 Config* config,
                 ControlInterface* control);
  ~ChromeosDaemon();

  // Runs the message loop.
  virtual void RunMessageLoop() = 0;

  // Starts the termination actions in the manager.
  void Quit();

 protected:
  Manager* manager() const { return manager_.get(); }

  void Start();

 private:
  friend class ChromeosDaemonTest;

  // Apply run-time settings to the manager.
  void ApplySettings(const Settings& settings);

  // Called when the termination actions are completed.
  void TerminationActionsCompleted(const Error& error);

  // Calls Stop() and then causes the dispatcher message loop to terminate and
  // return to the main function which started the daemon.
  void StopAndReturnToMain();

  void Stop();

  Config* config_;
  std::unique_ptr<ControlInterface> control_;
  EventDispatcher dispatcher_;
  GLib glib_;
  std::unique_ptr<Metrics> metrics_;
  RTNLHandler* rtnl_handler_;
  RoutingTable* routing_table_;
  DHCPProvider* dhcp_provider_;
#if !defined(DISABLE_WIFI)
  NetlinkManager* netlink_manager_;
  Callback80211Metrics callback80211_metrics_;
#endif  // DISABLE_WIFI
  std::unique_ptr<Manager> manager_;
};

}  // namespace shill

#endif  // SHILL_CHROMEOS_DAEMON_H_
