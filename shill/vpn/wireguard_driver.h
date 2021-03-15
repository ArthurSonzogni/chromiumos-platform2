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

  // These functions (including GetProvider() below) are overridden for
  // implementing the "Wireguard.Peers" property in both property store (as an
  // array of dicts) and storage (as an an array of json-encoded strings), and
  // its value is kept in |peers_| in this class. A special property in a peer
  // is "PresharedKey": this property cannot be read via RPC, so we need some
  // special handling during writing. Specifically, in a RPC call for setting
  // "Wireguard.Peers", the preshared key of a peer will not be cleared if the
  // client does not specify a value for it (i.e., the incoming request does not
  // contain this key).
  void InitPropertyStore(PropertyStore* store) override;
  bool Load(const StoreInterface* storage,
            const std::string& storage_id) override;
  bool Save(StoreInterface* storage,
            const std::string& storage_id,
            bool save_credentials) override;

 protected:
  KeyValueStore GetProvider(Error* error) override;

 private:
  // Friend class for testing.
  friend class WireguardDriverTestPeer;

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

  bool UpdatePeers(const Stringmaps& new_peers, Error* error);
  void ClearPeers(Error* error);

  Stringmaps peers_;

  EventHandler* event_handler_;
  pid_t wireguard_pid_ = -1;
  int interface_index_ = -1;
  IPConfig::Properties ip_properties_;
  base::FilePath config_file_;

  // The following two fields are constants. Makes them member variables for
  // testing.
  base::FilePath config_directory_;
  gid_t vpn_gid_;

  base::WeakPtrFactory<WireguardDriver> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_WIREGUARD_DRIVER_H_
