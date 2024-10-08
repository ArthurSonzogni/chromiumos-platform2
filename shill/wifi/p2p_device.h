// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_DEVICE_H_
#define SHILL_WIFI_P2P_DEVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/patchpanel/dbus/client.h>

#include "shill/network/network.h"
#include "shill/store/key_value_store.h"
#include "shill/supplicant/supplicant_event_delegate_interface.h"
#include "shill/supplicant/supplicant_group_event_delegate_interface.h"
#include "shill/supplicant/supplicant_p2pdevice_event_delegate_interface.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/p2p_peer.h"
#include "shill/wifi/p2p_service.h"
#include "shill/wifi/wifi_phy.h"

namespace shill {

class Manager;
class SupplicantInterfaceProxyInterface;
class SupplicantP2PDeviceProxyInterface;
class SupplicantGroupProxyInterface;

class P2PDevice : public LocalDevice,
                  public Network::EventHandler,
                  public SupplicantEventDelegateInterface,
                  public SupplicantGroupEventDelegateInterface,
                  public SupplicantP2PDeviceEventDelegateInterface {
 public:
  enum class P2PDeviceState {
    // Common states for all roles.
    kUninitialized,  // P2PDevice instance created, but no interface is created
                     // in kernel
    kReady,  // Any prerequisite steps (like connect to the primary interface,
             // get up to date phy info) are done on the device and can start
             // the P2P process

    // P2P client states.
    kClientAssociating,  // P2P client is connecting to a group
    kClientConfiguring,  // P2P client has joined an L2 P2P group and is setting
                         // up L3 connectivity
    kClientConnected,    // P2P client has joined a group and L3 link has been
                         // established
    kClientDisconnecting,  // P2P client is disconnecting from a group due to an
                           // API request
    kClientDisconnectingOnResourceBusy,  // P2P client is disconnecting from a
                                         // group due to a request triggered by
                                         // Shill due to resource constraints.

    // P2P GO states.
    kGOStarting,     // P2P GO is creating a group
    kGOConfiguring,  // P2P GO has created an L2 P2P group and is setting up L3
                     // network
    kGOActive,       // P2P GO has created a group and can accept connections
    kGOStopping,     // P2P GO is destroying a group
    kGOStoppingOnResourceBusy,  // P2P GO is destroying a group due to a request
                                // triggered by Shill due to resource
                                // constraints.
  };

  // Constructor function
  P2PDevice(Manager* manager,
            LocalDevice::IfaceType iface_type,
            const std::string& primary_link_name,
            uint32_t phy_index,
            int32_t shill_id,
            WiFiPhy::Priority priority,
            LocalDevice::EventCallback callback);

  P2PDevice(const P2PDevice&) = delete;
  P2PDevice& operator=(const P2PDevice&) = delete;

  ~P2PDevice() override;

  static const char* P2PDeviceStateName(P2PDeviceState state);

  // Get properties of group managed by this device (GO only).
  mockable KeyValueStore GetGroupInfo() const;

  // Get properties of client connection managed by this device (GC only).
  mockable KeyValueStore GetClientInfo() const;

  // P2PDevice start routine. Override the base class Start.
  bool Start() override;

  // P2PDevice stop routine. Override the base class Stop.
  bool Stop() override;

  // Return the configured service on this device.
  LocalService* GetService() const override { return service_.get(); }

  // Return the operating frequency of the group. Return std::nullopt if
  // P2P group is not active.
  std::optional<Integer> frequency() const override { return group_frequency_; }

  // Creates a P2P group with the current device as the group owner
  // using the setting from |service|. Functionality is stubbed.
  mockable bool CreateGroup(std::unique_ptr<P2PService> service);

  // Starts a P2P connection with a device |peer_address| with the
  // specified configuration in |service|. Functionality is stubbed.
  mockable bool Connect(std::unique_ptr<P2PService> service);

  // Removes the current P2P group. Functionality is stubbed.
  mockable bool RemoveGroup(bool resource_busy);

  // Disconnect a P2P connection with a device |peer_address|.
  // Functionality is stubbed.
  mockable bool Disconnect(bool resource_busy);

  // Get log name prefix.
  const std::string& log_name() const { return log_name_; }

  // Get shill_id_.
  int32_t shill_id() const { return shill_id_; }

  // Get state_.
  mockable P2PDeviceState state() const { return state_; }

  // Implementation of SupplicantEventDelegateInterface.  These methods
  // are called by SupplicantInterfaceProxy, in response to events from
  // wpa_supplicant.
  void PropertiesChanged(const KeyValueStore& properties) override {}
  void BSSAdded(const RpcIdentifier& BSS,
                const KeyValueStore& properties) override {}
  void BSSRemoved(const RpcIdentifier& BSS) override {}
  void Certification(const KeyValueStore& properties) override {}
  void EAPEvent(const std::string& status,
                const std::string& parameter) override {}
  void InterworkingAPAdded(const RpcIdentifier& BSS,
                           const RpcIdentifier& cred,
                           const KeyValueStore& properties) override {}
  void InterworkingSelectDone() override {}
  void ANQPQueryDone(const std::string& addr,
                     const std::string& result) override {}
  void ScanDone(const bool& success) override {}
  void StationAdded(const RpcIdentifier& Station,
                    const KeyValueStore& properties) override {}
  void StationRemoved(const RpcIdentifier& Station) override {}
  void PskMismatch() override {}
  void TermsAndConditions(const std::string& url) override {}

  // Implementation of SupplicantP2PDeviceEventDelegateInterface. These
  // methods are called by the P2PManager, in response to events from
  // wpa_supplicant.
  mockable void GroupStarted(const KeyValueStore& properties) override;
  mockable void GroupFinished(const KeyValueStore& properties) override;
  mockable void GroupFormationFailure(const std::string& reason) override;

  // Implementation of SupplicantGroupEventDelegateInterface. These methods
  // are called by SupplicantGroupProxy, in response to events from
  // wpa_supplicant.
  void PeerJoined(const dbus::ObjectPath& peer) override;
  void PeerDisconnected(const dbus::ObjectPath& peer) override;

 private:
  friend class P2PDeviceTest;
  FRIEND_TEST(P2PDeviceTest, DeviceOnOff);
  FRIEND_TEST(P2PDeviceTest, GroupInfo);
  FRIEND_TEST(P2PDeviceTest, GroupInfo_EmptyOnClient);
  FRIEND_TEST(P2PDeviceTest, ClientInfo);
  FRIEND_TEST(P2PDeviceTest, ClientInfo_EmptyOnGO);
  FRIEND_TEST(P2PDeviceTest, PeerJoinAndDisconnect);
  FRIEND_TEST(P2PDeviceTest, PeerJoinAndDisconnect_WhileNotReady);
  FRIEND_TEST(P2PDeviceTest, CreateAndRemove);
  FRIEND_TEST(P2PDeviceTest, ConnectAndDisconnect);
  FRIEND_TEST(P2PDeviceTest, BadState_GO);
  FRIEND_TEST(P2PDeviceTest, BadState_Client);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantInterfaceProxy);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantInterfaceProxy_WhileConnected);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantInterfaceProxy_Failure);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy_WhileConnected);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantP2PDeviceProxy_Failure);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantGroupProxy);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantGroupProxy_WhileConnected);
  FRIEND_TEST(P2PDeviceTest, ConnectToSupplicantGroupProxy_Failure);
  FRIEND_TEST(P2PDeviceTest, SetupGroup);
  FRIEND_TEST(P2PDeviceTest, SetupGroup_EmptyProperties);
  FRIEND_TEST(P2PDeviceTest, SetupGroup_MissingGroupPath);
  FRIEND_TEST(P2PDeviceTest, GroupStarted_WhileNotExpected);
  FRIEND_TEST(P2PDeviceTest, GroupFinished_WhileGOStarting);
  FRIEND_TEST(P2PDeviceTest, GroupFinished_WhileGOConfiguring);
  FRIEND_TEST(P2PDeviceTest, GroupFinished_WhileGOActive);
  FRIEND_TEST(P2PDeviceTest, GroupFinished_WhileClientAssociating);
  FRIEND_TEST(P2PDeviceTest, GroupFinished_WhileClientConfiguring);
  FRIEND_TEST(P2PDeviceTest, GroupFinished_WhileClientConnected);
  FRIEND_TEST(P2PDeviceTest, GroupFinished_WhileNotExpected);
  FRIEND_TEST(P2PDeviceTest, StartingTimerExpired_WhileGOStarting);
  FRIEND_TEST(P2PDeviceTest, StartingTimerExpired_WhileGOConfiguring);
  FRIEND_TEST(P2PDeviceTest, DISABLED_StartingTimerExpired_WhileGOConfiguring);
  FRIEND_TEST(P2PDeviceTest, StartingTimerExpired_WhileGOActive);
  FRIEND_TEST(P2PDeviceTest, StoppingTimerExpired_WhileGOStopping);
  FRIEND_TEST(P2PDeviceTest, StartingTimerExpired_WhileClientAssociating);
  FRIEND_TEST(P2PDeviceTest, StartingTimerExpired_WhileClientConfiguring);
  FRIEND_TEST(P2PDeviceTest,
              DISABLED_StartingTimerExpired_WhileClientConfiguring);
  FRIEND_TEST(P2PDeviceTest, StartingTimerExpired_WhileClientConnected);
  FRIEND_TEST(P2PDeviceTest, StoppingTimerExpired_WhileClientDisconnecting);
  FRIEND_TEST(P2PDeviceTest, GO_StartGroupNetworkImmediateFail);
  FRIEND_TEST(P2PDeviceTest, GO_StartGroupNetworkFail);
  FRIEND_TEST(P2PDeviceTest, Client_AcquireClientIPFail);
  FRIEND_TEST(P2PDeviceTest, Client_NetworkStopped);

  // This helper method converts GO peer properties to D-Bus properties.
  Stringmaps GroupInfoClients() const;

  // Set service_ to |service|.
  void SetService(std::unique_ptr<P2PService> service);

  // Delete service_.
  void DeleteService();

  // Set P2PDeviceState.
  void SetState(P2PDeviceState state);

  // Return true if link layer has connected.
  bool IsLinkLayerConnected() const;

  // Return true if network layer has connected.
  bool IsNetworkLayerConnected() const;

  // Returns true if the device is in an active GO state.
  bool InGOState() const;

  // Returns true if the device is in an active Client state.
  bool InClientState() const;

  // Connect to wpa_supplicant interface proxy of interface object received
  // on GroupStarted signal. It's link name is used as P2PDevice link name.
  bool ConnectToSupplicantInterfaceProxy(const RpcIdentifier& interface);

  // Disconnect from wpa_supplicant interface proxy on GroupFinished signal.
  void DisconnectFromSupplicantInterfaceProxy();

  // This helper method retrieves P2PDevice link name via wpa_supplicant
  // interface proxy.
  String GetInterfaceName() const;

  // This helper method retrieves P2PDevice MAC address via wpa_supplicant
  // interface proxy.
  std::optional<net_base::MacAddress> GetInterfaceAddress() const;

  // Connect to wpa_supplicant p2p device proxy of interface object received
  // on GroupStarted signal.
  bool ConnectToSupplicantP2PDeviceProxy(const RpcIdentifier& interface);

  // Disconnect from wpa_supplicant p2p device proxy on GroupFinished signal.
  void DisconnectFromSupplicantP2PDeviceProxy();

  // Connect to wpa_supplicant group proxy of group object received
  // on GroupStarted signal. It provides the following wpa_supplicant
  // signals: PeerJoined and PeerDisconnected.
  bool ConnectToSupplicantGroupProxy(const RpcIdentifier& group);

  // Disconnect from wpa_supplicant group proxy on GroupFinished signal.
  void DisconnectFromSupplicantGroupProxy();

  // These helper methods are used to retrieve group properties
  // via wpa_supplicant group proxy.
  String GetGroupSSID() const;
  std::optional<net_base::MacAddress> GetGroupBSSID() const;
  Integer GetGroupFrequency() const;
  String GetGroupPassphrase() const;

  // Returns wpa_supplicant p2p device proxy of the primary network interface.
  SupplicantP2PDeviceProxyInterface* SupplicantPrimaryP2PDeviceProxy() const;

  // These helper methods delegate group start/finish requests from P2PManager
  // to the wpa_supplicant D-Bus method calls. On success they result with
  // GroupStarted/GroupFinished signal, respectively.
  bool StartSupplicantGroupForGO(const KeyValueStore& properties);
  bool StartSupplicantGroupForClient(const KeyValueStore& properties);
  bool FinishSupplicantGroup();

  // These helper methods provide final operations for group setup/teardown
  // which are executed on wpa_supplicant GroupStarted/GroupFinished signal,
  // respectively.
  bool SetupGroup(const KeyValueStore& properties);
  void TeardownGroup(const KeyValueStore& properties);
  void TeardownGroup();

  // These helper methods provide operation required for network setup/teardown.
  // Depending on device role, they may be called in response to events either
  // from patchpanel or Shill::Network, in case of GO and Client, respectively.
  void AcquireClientIP();
  bool StartGroupNetwork();
  void OnGroupNetworkStarted(
      base::ScopedFD network_fd,
      const patchpanel::Client::DownstreamNetwork& network);
  void NetworkFinished();
  void NetworkFailure(const std::string& reason);

  // Overrides for Network::EventHandler.
  void OnConnectionUpdated(int interface_index) override;
  void OnNetworkStopped(int interface_index, bool is_failure) override;

  // Methods to fetch L3 information from patchpanel.
  void UpdateGroupNetworkInfo(
      const patchpanel::Client::DownstreamNetwork& downstream_network);

  // Primary interface link name.
  std::string primary_link_name_;

  // Name used for logging.
  std::string log_name_;
  // Uniquely identifies this device relative to all other P2P devices in Shill.
  int32_t shill_id_;
  // P2P device state as listed in enum P2PDeviceState.
  P2PDeviceState state_;
  // P2P service configured on this device.
  std::unique_ptr<P2PService> service_;

  // The wpa_supplicant interface proxy of the p2p network interface created
  // for wifi direct connectivity. It is initialized on GroupStarted signal
  // via ConnectToSupplicantInterfaceProxy() and destroyed on GroupFinished
  // signal via DisconnectFromSupplicantInterfaceProxy().
  std::unique_ptr<SupplicantInterfaceProxyInterface>
      supplicant_interface_proxy_;

  // The wpa_supplicant interface object path.
  RpcIdentifier supplicant_interface_path_;

  // The wpa_supplicant p2p device proxy of the p2p network interface created
  // for wifi direct connectivity. It provides group Disconnect method.
  // It is initialized on GroupStarted signal via ConnectP2PDeviceProxy()
  // and destroyed on GroupFinished signal via DisconnectP2PDeviceProxy()
  std::unique_ptr<SupplicantP2PDeviceProxyInterface>
      supplicant_p2pdevice_proxy_;

  // The wpa_supplicant group proxy. It provides the following signals:
  // PeerJoined and PeerDisconnected. Initialized by ConnectP2PGroupProxy().
  std::unique_ptr<SupplicantGroupProxyInterface> supplicant_group_proxy_;

  // The wpa_supplicant group object path.
  RpcIdentifier supplicant_group_path_;

  // The wpa_supplicant group properties.
  String group_ssid_;
  std::optional<net_base::MacAddress> group_bssid_;
  std::optional<Integer> group_frequency_;
  String group_passphrase_;

  // The wpa_supplicant persistent group path used for p2p client connection.
  RpcIdentifier supplicant_persistent_group_path_;

  // Map of associated P2P peers.
  std::map<dbus::ObjectPath, std::unique_ptr<P2PPeer>> group_peers_;

  // File descriptor representing the group network setup managed by
  // patchpanel. Closing this file descriptor tears down the group network.
  // This member is only valid when |this| is in the GO role.
  base::ScopedFD group_network_fd_;

  // Pointer to the Network class which maintains the layer 3 configuration.
  // This member is only valid when |this| is in the client role.
  std::unique_ptr<Network> client_network_;
  std::unique_ptr<Network> client_network_for_test_;

  // P2P interface address, only available after the link layer is connected.
  std::optional<net_base::MacAddress> interface_address_;
  // IPv4 address of current device, only available for GO after the network
  // layer has been setup.
  std::optional<net_base::IPv4Address> go_ipv4_address_;
  // network_id of current device, only available for GO after the network layer
  // has been setup.
  std::optional<int> go_network_id_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_DEVICE_H_
