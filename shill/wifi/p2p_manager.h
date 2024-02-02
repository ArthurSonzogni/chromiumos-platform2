// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_MANAGER_H_
#define SHILL_WIFI_P2P_MANAGER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/error.h"
#include "shill/store/key_value_store.h"
#include "shill/store/property_store.h"
#include "shill/supplicant/supplicant_p2pdevice_event_delegate_interface.h"
#include "shill/wifi/p2p_device.h"

namespace shill {

class Manager;
class StoreInterface;
class SupplicantP2PDeviceProxyInterface;

// P2PManager handles P2P related logic. It is created by the Manager class.
class P2PManager : public SupplicantP2PDeviceEventDelegateInterface {
 public:
  explicit P2PManager(Manager* manager);
  P2PManager(const P2PManager&) = delete;
  P2PManager& operator=(const P2PManager&) = delete;

  virtual ~P2PManager();

  // Initialize D-Bus properties related to P2P.
  void InitPropertyStore(PropertyStore* store);
  // Start P2PManager.
  void Start();
  // Stop P2PManager.
  void Stop();

  // The following methods are used to handle P2P-related actions.

  // Create a new P2P group-owner mode interface and initialize a P2P group on
  // that interface.
  void CreateP2PGroup(base::OnceCallback<void(KeyValueStore result)> callback,
                      const KeyValueStore& args);

  // Creates a P2P client mode interface to and connects it to an existing P2P
  // group.
  void ConnectToP2PGroup(
      base::OnceCallback<void(KeyValueStore result)> callback,
      const KeyValueStore& args);

  // Destroy the existing P2P group and tear down the P2P group-owner interface.
  void DestroyP2PGroup(base::OnceCallback<void(KeyValueStore result)> callback,
                       int shill_id);

  // Disconnect from a P2P group. Will destroy the P2P client interface.
  void DisconnectFromP2PGroup(
      base::OnceCallback<void(KeyValueStore result)> callback, int shill_id);

  // D-Bus property getters
  // This property is temporary and will be removed when the feature is mature.
  bool allowed() const { return allowed_; }

  // Implementation of SupplicantP2PDeviceEventDelegateInterface. These
  // methods are called by SupplicantP2PDeviceProxy, in response to events
  // from wpa_supplicant.
  void GroupStarted(const KeyValueStore& properties) override;
  void GroupFinished(const KeyValueStore& properties) override;
  void GroupFormationFailure(const std::string& reason) override;

  // This returns wpa_supplicant p2p device proxy owned by P2PManager.
  mockable SupplicantP2PDeviceProxyInterface* SupplicantPrimaryP2PDeviceProxy()
      const {
    return supplicant_primary_p2pdevice_proxy_.get();
  }

 private:
  friend class P2PManagerTest;
  FRIEND_TEST(P2PManagerTest, SetP2PAllowed);
  FRIEND_TEST(P2PManagerTest, GetP2PCapabilities);
  FRIEND_TEST(P2PManagerTest, GetP2PGroupInfos);
  FRIEND_TEST(P2PManagerTest, GetP2PClientInfos);
  FRIEND_TEST(P2PManagerTest, ConnectAndDisconnectClient);
  FRIEND_TEST(P2PManagerTest, CreateAndDestroyGroup);
  FRIEND_TEST(P2PManagerTest, DisconnectWithoutConnect);
  FRIEND_TEST(P2PManagerTest, DestroyWithoutCreate);
  FRIEND_TEST(P2PManagerTest, ShillIDs);
  FRIEND_TEST(P2PManagerTest, MissingArgs_CreateGroup);
  FRIEND_TEST(P2PManagerTest, MissingArgs_ConnectClient);
  FRIEND_TEST(P2PManagerTest, GroupStarted);
  FRIEND_TEST(P2PManagerTest, GroupStarted_IgnoreDuplicates);
  FRIEND_TEST(P2PManagerTest, GroupStarted_IgnoreMissingDevice);
  FRIEND_TEST(P2PManagerTest, GroupStarted_IgnoreMissingProperties);
  FRIEND_TEST(P2PManagerTest, GroupFinished);
  FRIEND_TEST(P2PManagerTest, GroupFinished_BeforeStarted);
  FRIEND_TEST(P2PManagerTest, GroupFinished_IgnoreDuplicates);
  FRIEND_TEST(P2PManagerTest, GroupFinished_IgnoreMissingDevice);
  FRIEND_TEST(P2PManagerTest, GroupFinished_IgnoreMissingProperties);
  FRIEND_TEST(P2PManagerTest, GroupFormationFailure);
  FRIEND_TEST(P2PManagerTest, GroupFormationFailure_IgnoreDuplicates);
  FRIEND_TEST(P2PManagerTest, GroupFormationFailure_IgnoreMissingDevice);

  // This checks whether the platform supports P2P operations.
  bool IsP2PSupported();

  // This checks whether the platform is currently able to support
  // a new P2P Group Owner interface.
  String GroupReadiness();

  // This checks whether the platform is currently able to support
  // a new P2P Client interface.
  String ClientReadiness();

  // This provides the list of supported channel frequencies in MHZ.
  Integers SupportedChannels();

  // This provides a list of channels that the platform would prefer
  // the P2P link to be created on.
  Integers PreferredChannels();

  using P2PResultCallback = base::OnceCallback<void(KeyValueStore result)>;

  void HelpRegisterDerivedBool(PropertyStore* store,
                               std::string_view name,
                               bool (P2PManager::*get)(Error* error),
                               bool (P2PManager::*set)(const bool&, Error*));

  void HelpRegisterDerivedKeyValueStore(
      PropertyStore* store,
      std::string_view name,
      KeyValueStore (P2PManager::*get)(Error* error),
      bool (P2PManager::*set)(const KeyValueStore&, Error*));

  void HelpRegisterDerivedKeyValueStores(
      PropertyStore* store,
      std::string_view name,
      KeyValueStores (P2PManager::*get)(Error* error),
      bool (P2PManager::*set)(const KeyValueStores&, Error*));

  // D-Bus accessors.
  bool SetAllowed(const bool& value, Error* error);
  bool GetAllowed(Error* /*error*/) { return allowed_; }

  // P2P properties get handlers.
  KeyValueStore GetCapabilities(Error* error);
  KeyValueStores GetGroupInfos(Error* error);
  KeyValueStores GetClientInfos(Error* error);

  // P2P device event handler.
  void OnP2PDeviceEvent(LocalDevice::DeviceEvent event,
                        const LocalDevice* device);

  // Stubbed method for handling creation of a Group Owner device.
  void OnP2PDeviceEnabled(P2PDeviceRefPtr device) { return; }

  // Method for handling of the creation of an L3 network for a P2P Group.
  void P2PNetworkStarted(P2PDeviceRefPtr device);

  // Stubbed method for handling a peer associating to the P2PGroup.
  void OnPeerAssoc(P2PDeviceRefPtr device) { return; }

  // Stubbed method for handling a peer disassociating from the P2PGroup.
  void OnPeerDisassoc(P2PDeviceRefPtr device) { return; }

  void PostResult(std::string result_code,
                  std::optional<uint32_t> shill_id,
                  P2PResultCallback callback);

  // Delete a P2P device, stopping all active operations and deleting it's
  // references.
  void DeleteP2PDevice(P2PDeviceRefPtr p2p_dev_);

  // This returns link name of the primary interface.
  std::string PrimaryLinkName() const;

  // This returns wpa_supplicant process proxy.
  SupplicantProcessProxyInterface* SupplicantProcessProxy() const;

  // This returns wpa_supplicant D-Bus control interface.
  ControlInterface* ControlInterface() const;

  // Connect to wpa_supplicant p2p device proxy of the primary interface.
  // The primary interface is also created if it wasn't already controlled
  // by wpa_supplicant before.
  bool ConnectToSupplicantPrimaryP2PDeviceProxy();

  // Disconnect from wpa_supplicant device proxy of the primary interface.
  void DisconnectFromSupplicantPrimaryP2PDeviceProxy();

  // Reference to the main Shill Manager instance. P2PManager is created and
  // owned by WiFiProvider, which can be accessed indirectly through manager_.
  Manager* manager_;
  // P2P feature flag.
  bool allowed_;

  // Map of unique IDs to P2P group owners.
  std::map<uint32_t, P2PDeviceRefPtr> p2p_group_owners_;
  // Map of unique IDs to P2P clients.
  std::map<uint32_t, P2PDeviceRefPtr> p2p_clients_;

  // The next value that should be used as a unique ID for a P2P device.
  // Increases by 1 for each new device and resets to 0 when P2PManager is
  // reset.
  uint32_t next_unique_id_;

  // Member to hold the result callback function. This callback function gets
  // set when dbus methods (CreateP2PGroup, ConnectToP2PGroup, DestroyP2PGroup,
  // DisconnectFromP2PGroup) are called and runs when the async method call is
  // done.
  P2PResultCallback result_callback_;

  // The wpa_supplicant p2p device proxy of the primary network interface.
  // It provides group status signals which are handled by P2PManager and
  // then delegated to proper P2PDevice. It also provides group configuration
  // methods which are used directly by P2PDevice.
  std::unique_ptr<SupplicantP2PDeviceProxyInterface>
      supplicant_primary_p2pdevice_proxy_;

  // The wpa_supplicant event delegate object of pending P2PDevice.
  SupplicantP2PDeviceEventDelegateInterface*
      supplicant_primary_p2pdevice_pending_event_delegate_;

  // Map of unique wpa_supplicant interface object paths to associated
  // event delegate objects (active P2PDevices).
  std::map<RpcIdentifier, SupplicantP2PDeviceEventDelegateInterface*>
      supplicant_primary_p2pdevice_event_delegates_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_MANAGER_H_
