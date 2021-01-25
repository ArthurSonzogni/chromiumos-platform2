// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_WIREGUARD_DRIVER_H_
#define SHILL_VPN_WIREGUARD_DRIVER_H_

#include <string>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/ipconfig.h"
#include "shill/vpn/vpn_driver.h"

namespace shill {

class WireguardDriver : public VPNDriver {
 public:
  WireguardDriver(Manager* manager, ProcessManager* process_manager);
  WireguardDriver(const WireguardDriver&) = delete;
  WireguardDriver& operator=(const WireguardDriver&) = delete;

  ~WireguardDriver();

  // Inherited from VPNDriver.
  base::TimeDelta ConnectAsync(EventHandler* event_handler) override;
  void Disconnect() override;
  void OnConnectTimeout() override;
  IPConfig::Properties GetIPProperties() const override;
  std::string GetProviderType() const override;

 private:
  static const VPNDriver::Property kProperties[];

  // Called in ConnectAsync() by PostTask(), to make sure the connect procedure
  // is executed asynchronously.
  void ConnectInternal();

  // Spawns the userspace wireguard process, which will setup the tunnel
  // interface and do the data tunneling. WireguardProcessExited() will be
  // invoked if that process exits unexpectedly.
  bool SpawnWireguard();
  void WireguardProcessExited(int exit_code);

  // Configures the interface via wireguard-tools when the interface is ready.
  // This function is used as a DeviceInfo::LinkReadyCallback so it has two
  // parameters, although we don't actually need them.
  void ConfigureInterface(const std::string& interface_name,
                          int interface_index);

  // Calls Cleanup(), and if there is a service associated through
  // ConnectAsync(), notifies it of the failure.
  void FailService(Service::ConnectFailure failure,
                   const std::string& error_details);
  // Resets states and deallocate all resources.
  void Cleanup();

  EventHandler* event_handler_;
  pid_t wireguard_pid_ = -1;

  base::WeakPtrFactory<WireguardDriver> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_WIREGUARD_DRIVER_H_
