// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_WIREGUARD_DRIVER_H_
#define SHILL_VPN_WIREGUARD_DRIVER_H_

#include <memory>
#include <string>
#include <string_view>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/process_manager.h>

#include "shill/metrics.h"
#include "shill/vpn/vpn_driver.h"
#include "shill/vpn/vpn_end_reason.h"
#include "shill/vpn/vpn_util.h"

namespace shill {

class WireGuardDriver : public VPNDriver {
 public:
  WireGuardDriver(Manager* manager, net_base::ProcessManager* process_manager);
  WireGuardDriver(const WireGuardDriver&) = delete;
  WireGuardDriver& operator=(const WireGuardDriver&) = delete;

  ~WireGuardDriver();

  // Inherited from VPNDriver.
  base::TimeDelta ConnectAsync(EventHandler* event_handler) override;
  void Disconnect() override;
  void OnConnectTimeout() override;
  std::unique_ptr<net_base::NetworkConfig> GetNetworkConfig() const override;

  // These functions (including GetProvider() below) are overridden for
  // implementing the "WireGuard.Peers" property in both property store (as an
  // array of dicts) and storage (as an an array of json-encoded strings), and
  // its value is kept in |peers_| in this class. A special property in a peer
  // is "PresharedKey": this property cannot be read via RPC, so we need some
  // special handling during writing. Specifically, in a RPC call for setting
  // "WireGuard.Peers", the preshared key of a peer will not be cleared if the
  // client does not specify a value for it (i.e., the incoming request does not
  // contain this key).
  void InitPropertyStore(PropertyStore* store) override;
  bool Load(const StoreInterface* storage,
            const std::string& storage_id) override;
  // Save() will also trigger the key-pair generation if the private key is
  // empty. Given that Save() will always be called after any property changes
  // by Manager::ConfigureService*(), this guarantees that there will always be
  // a valid key pair in the service.
  bool Save(StoreInterface* storage,
            const std::string& storage_id,
            bool save_credentials) override;
  // Resets credential fields (PrivateKey and PresharedKey) from the service.
  void UnloadCredentials() override;

  static bool IsSupported();

 protected:
  KeyValueStore GetProvider(Error* error) override;

 private:
  // Friend class for testing.
  friend class WireGuardDriverTestPeer;

  static const VPNDriver::Property kProperties[];

  void CreateKernelWireGuardInterface();

  // Generates the contents for the config file that will be used by
  // wireguard-tools from the profile. Returns an empty string on failure.
  std::string GenerateConfigFileContents();

  // Configures the interface via wireguard-tools when the interface is ready.
  void ConfigureInterface(const std::string& interface_name,
                          int interface_index);
  void OnConfigurationDone(int exit_code);

  // Fills in |ipv4_properties_| (especially, the address and routes fields)
  // according to the properties in the profile.
  bool PopulateIPProperties();

  // Functions to execute `wg show` to read the connection status, and update
  // the "Provider" property to include the information about latest handshake
  // time, and tx and rx bytes.
  void ScheduleNextReadLinkStatus(base::TimeDelta delay);
  void ReadLinkStatus();
  void OnReadLinkStatusDone(int exit_status, const std::string& output);

  // Calls Cleanup(), and if there is a service associated through
  // ConnectAsync(), notifies it of the failure.
  void FailService(VPNEndReason failure, std::string_view error_details);
  // Resets states and deallocate all resources.
  void Cleanup();

  bool UpdatePeers(const Stringmaps& new_peers, Error* error);
  void ClearPeers(Error* error);

  void ReportConnectionMetrics();

  Stringmaps peers_;

  EventHandler* event_handler_;
  pid_t wireguard_pid_ = -1;
  int interface_index_ = -1;
  std::optional<net_base::NetworkConfig> network_config_;
  base::ScopedFD config_fd_;

  // Indicates that whether we have an open wg interface in the kernel which is
  // created via DeviceInfo now.
  bool kernel_interface_open_ = false;

  // This variable is set in Load() and Save(), and only used to check whether
  // we need to re-calculate the public key in Save().
  std::string saved_private_key_;

  // Indicates where the key pair associated with this service comes from.
  // Currently only used in the UMA metrics.
  Metrics::VpnWireGuardKeyPairSource key_pair_source_;

  std::unique_ptr<VPNUtil> vpn_util_;

  // Tasks for running `wg show` are bound to this weak factory, so that they
  // can be cancelled easily.
  base::WeakPtrFactory<WireGuardDriver> weak_factory_for_read_link_status_{
      this};

  base::WeakPtrFactory<WireGuardDriver> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_WIREGUARD_DRIVER_H_
