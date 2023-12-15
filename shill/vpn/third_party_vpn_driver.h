// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_THIRD_PARTY_VPN_DRIVER_H_
#define SHILL_VPN_THIRD_PARTY_VPN_DRIVER_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <gtest/gtest_prod.h>
#include <net-base/ip_address.h>
#include <net-base/network_config.h>
#include <net-base/process_manager.h>

#include "shill/ipconfig.h"
#include "shill/vpn/vpn_driver.h"

namespace shill {

class Error;
class FileIO;
class ThirdPartyVpnAdaptorInterface;

class ThirdPartyVpnDriver : public VPNDriver {
 public:
  enum PlatformMessage {
    kConnected = 1,
    kDisconnected,
    kError,
    kLinkDown,
    kLinkUp,
    kLinkChanged,
    kSuspend,
    kResume
  };

  ThirdPartyVpnDriver(Manager* manager,
                      net_base::ProcessManager* process_manager);
  ThirdPartyVpnDriver(const ThirdPartyVpnDriver&) = delete;
  ThirdPartyVpnDriver& operator=(const ThirdPartyVpnDriver&) = delete;

  ~ThirdPartyVpnDriver() override;

  // UpdateConnectionState is called by DBus adaptor when
  // "UpdateConnectionState" method is called on the DBus interface.
  void UpdateConnectionState(Service::ConnectState connection_state,
                             std::string* error_message);

  // SendPacket is called by the DBus adaptor when "SendPacket" method is called
  // on the DBus interface.
  void SendPacket(const std::vector<uint8_t>& data, std::string* error_message);

  // SetParameters is called by the DBus adaptor when "SetParameter" method is
  // called on the DBus interface.
  void SetParameters(const std::map<std::string, std::string>& parameters,
                     std::string* error_message,
                     std::string* warning_message);

  void ClearExtensionId(Error* error);
  bool SetExtensionId(const std::string& value, Error* error);

  // Implementation of VPNDriver
  void InitPropertyStore(PropertyStore* store) override;
  base::TimeDelta ConnectAsync(EventHandler* handler) override;
  std::unique_ptr<net_base::NetworkConfig> GetNetworkConfig() const override;
  void Disconnect() override;
  void OnConnectTimeout() override;

  void OnDefaultPhysicalServiceEvent(
      DefaultPhysicalServiceEvent event) override;

  bool Load(const StoreInterface* storage,
            const std::string& storage_id) override;
  bool Save(StoreInterface* storage,
            const std::string& storage_id,
            bool save_credentials) override;

  void OnBeforeSuspend(ResultCallback callback) override;
  void OnAfterResume() override;

  const std::string& object_path_suffix() const { return object_path_suffix_; }

 private:
  friend class ThirdPartyVpnDriverTest;
  FRIEND_TEST(ThirdPartyVpnDriverTest, ConnectAndDisconnect);
  FRIEND_TEST(ThirdPartyVpnDriverTest, ReconnectionEvents);
  FRIEND_TEST(ThirdPartyVpnDriverTest, PowerEvents);
  FRIEND_TEST(ThirdPartyVpnDriverTest, OnConnectTimeout);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SetParametersCorrect);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SetParametersDNSServers);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SetParametersExclusionList);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SetParametersDomainSearch);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SetParametersReconnect);
  FRIEND_TEST(ThirdPartyVpnDriverTest, UpdateConnectionState);
  FRIEND_TEST(ThirdPartyVpnDriverTest, SendPacket);

  // Resets the internal state and deallocates all resources - closes the
  // handle to tun device, IO handler if open and deactivates itself with the
  // |thirdpartyvpn_adaptor_| if active.
  void Cleanup();

  // First do Cleanup(). Then if there's a service associated through
  // ConnectAsync, notify it to sets its state to Service::kStateFailure, sets
  // the failure reason to |failure|, sets its ErrorDetails property to
  // |error_details|, and disassociates from the service.
  void FailService(Service::ConnectFailure failure,
                   std::string_view error_details);

  void OnLinkReady(const std::string& link_name, int interface_index);

  // These functions are called whe there is input and error in the tun
  // interface.
  void OnTunReadable();
  void OnInput(base::span<const uint8_t> data);

  static const Property kProperties[];

  // This variable keeps track of the active instance. There can be multiple
  // instance of this class at a time but only one would be active that can
  // communicate with the VPN client over DBUS.
  static ThirdPartyVpnDriver* active_client_;

  // ThirdPartyVpnAdaptorInterface manages the DBus communication and provides
  // an unique identifier for the ThirdPartyVpnDriver.
  std::unique_ptr<ThirdPartyVpnAdaptorInterface> adaptor_interface_;

  // Object path suffix is made of Extension ID and name that collectively
  // identifies the configuration of the third party VPN client.
  std::string object_path_suffix_;

  // File descriptor for the tun device.
  int tun_fd_;
  // Watcher to wait for |tun_fd_| ready to read. It should be destructed
  // prior than |tun_fd_| is closed.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> tun_watcher_;

  // Network configuration of the virtual VPN device set by the VPN client.
  std::optional<net_base::NetworkConfig> network_config_;
  bool network_config_set_;

  // The object is used to write to tun device.
  FileIO* file_io_;

  // The boolean indicates if parameters are expected from the VPN client.
  bool parameters_expected_;

  // Flag indicating whether the extension supports reconnections - a feature
  // that wasn't in the original API.  If not, we won't send link_* or
  // suspend/resume signals.
  bool reconnect_supported_;

  EventHandler* event_handler_ = nullptr;

  std::string interface_name_;
  int interface_index_ = -1;

  base::WeakPtrFactory<ThirdPartyVpnDriver> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_THIRD_PARTY_VPN_DRIVER_H_
