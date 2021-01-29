// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_WIREGUARD_DRIVER_H_
#define SHILL_VPN_WIREGUARD_DRIVER_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
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

  // Generates a config file that will be used by wireguard-tools from the
  // profile and write its content into a temporary file. Writes the path to the
  // temporary file into |config_file_|;
  bool GenerateConfigFile();
  // Called by GenerateConfigFile(). Reads the value of |key_in_args| from the
  // profile, and then append a line of "|key_in_config|=|value|" into lines.
  // Returns false if |is_required| is true and the corresponding value does not
  // exist or is empty.
  bool AppendConfig(const std::string& key_in_config,
                    const std::string& key_in_args,
                    bool is_required,
                    std::vector<std::string>* lines);

  // Configures the interface via wireguard-tools when the interface is ready.
  void ConfigureInterface(const std::string& interface_name,
                          int interface_index);
  void OnConfigurationDone(int exit_code);

  // Fills in |ip_properties_| (especially, the address and routes fields)
  // according to the properties in the profile.
  bool PopulateIPProperties();

  // Calls Cleanup(), and if there is a service associated through
  // ConnectAsync(), notifies it of the failure.
  void FailService(Service::ConnectFailure failure,
                   const std::string& error_details);
  // Resets states and deallocate all resources.
  void Cleanup();

  EventHandler* event_handler_;
  pid_t wireguard_pid_ = -1;
  int interface_index_ = -1;
  IPConfig::Properties ip_properties_;
  base::FilePath config_file_;

  base::WeakPtrFactory<WireguardDriver> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_WIREGUARD_DRIVER_H_
