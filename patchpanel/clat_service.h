// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CLAT_SERVICE_H_
#define PATCHPANEL_CLAT_SERVICE_H_

#include <optional>
#include <string>

#include <net-base/ipv6_address.h>
#include <net-base/process_manager.h>

#include "patchpanel/datapath.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

// This class configures, starts ortstops CLAT on ChromeOS host when the main
// Manager process notifies this class about changes on either the default
// logical device or IPConfig of it.
class ClatService {
 public:
  ClatService(Datapath* datapath,
              net_base::ProcessManager* process_manager,
              System* system);
  ClatService(const ClatService&) = delete;
  ClatService& operator=(const ClatService&) = delete;

  virtual ~ClatService();

  // Enable or disable the CLAT feature. `Disable()` calls `StopClat()` to clean
  // up the effects of ClatService if exist.
  void Enable();
  void Disable();
  bool is_enabled() const { return is_enabled_; }

  // Processes changes in the default logical shill device.
  // This function judges whether CLAT is needed, and based on that decision it
  // will do one of the following operations: start CLAT, stop CLAT, reconfigure
  // and restart CLAT, or do nothing.
  void OnShillDefaultLogicalDeviceChanged(
      const ShillClient::Device* new_device,
      const ShillClient::Device* prev_device);

  // Processes changes in IPConfig of the default logical shill device.
  // This function judges whether CLAT is needed, and based on that decision it
  // will do one of the following operations: start CLAT, stop CLAT, reconfigure
  // and restart CLAT, or do nothing.
  void OnDefaultLogicalDeviceIPConfigChanged(
      const ShillClient::Device& default_logical_device);

 protected:
  // This function does the followings step by step: create a config file for
  // TAYGA, create a tun device, start TAYGA's process, add NDproxy, add IPRule
  // and IPRoute.
  virtual void StartClat(const ShillClient::Device& shill_device);

  // This function does the followings step by step: remove IPRule and IPRoute,
  // remove NDproxy, kill TAYGA's process, remove the tun device used for CLAT.
  virtual void StopClat(bool clear_running_device = true);

  // These functions set or reset |clat_running_device_| in unit tests
  // respectively.
  void SetClatRunningDeviceForTest(const ShillClient::Device& shill_device);
  void ResetClatRunningDeviceForTest();

 private:
  bool IsClatRunningDevice(const ShillClient::Device& shill_device);

  // Creates a config file /run/tayga/tayga.conf. An old config file will be
  // overwritten by a new one.
  bool CreateConfigFile(const std::string& ifname,
                        const net_base::IPv6Address& clat_ipv6_addr);
  bool StartTayga();
  void StopTayga();

  // These variables are injected at the constructor. The caller should
  // guarantee these variables outlive the ClatService instance.
  Datapath* datapath_;
  net_base::ProcessManager* process_manager_;
  System* system_;
  // Flag to turn CLAT feature on or off. Can be modified through Enable() and
  // Disable().
  bool is_enabled_ = true;
  pid_t tayga_pid_ = -1;

  // The device on which CLAT should be running.
  // If CLAT is enabled, this has a value when CLAT is actually running. If
  // disabled, this has a value when the default logical device is IPv6-only.
  std::optional<ShillClient::Device> clat_running_device_;
  // IPv6 address used for address translation between IPv4 and IPv6 in CLAT.
  // This will be the source address of outgoing packets and destination address
  // of incoming packets in IPv6-only network. This has a value when CLAT is
  // actually running. i.e. this is std::nullopt while CLAT is disabled.
  std::optional<net_base::IPv6Address> clat_ipv6_addr_;
};
}  // namespace patchpanel

#endif  // PATCHPANEL_CLAT_SERVICE_H_
