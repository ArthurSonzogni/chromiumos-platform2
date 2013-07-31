// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIMAX_PROVIDER_H_
#define SHILL_WIMAX_PROVIDER_H_

#include <map>

#include <base/basictypes.h>
#include <base/memory/scoped_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/accessor_interface.h"
#include "shill/dbus_manager.h"
#include "shill/provider.h"
#include "shill/refptr_types.h"
#include "shill/wimax_network_proxy_interface.h"

namespace shill {

class ControlInterface;
class EventDispatcher;
class KeyValueStore;
class Manager;
class Metrics;
class ProxyFactory;
class WiMaxManagerProxyInterface;

class WiMaxProvider : public Provider {
 public:
  WiMaxProvider(ControlInterface *control,
                EventDispatcher *dispatcher,
                Metrics *metrics,
                Manager *manager);
  virtual ~WiMaxProvider();

  // Called by Manager as a part of the Provider interface.  The attributes
  // used for matching services for the WiMax provider are the NetworkId,
  // mode and Name parameters.
  virtual void CreateServicesFromProfile(const ProfileRefPtr &profile) override;
  virtual ServiceRefPtr GetService(const KeyValueStore &args,
                                   Error *error) override;
  void Start() override;
  void Stop() override;

  // Signaled by DeviceInfo when a new WiMAX device becomes available.
  virtual void OnDeviceInfoAvailable(const std::string &link_name);

  // Signaled by a WiMAX device when its set of live networks changes.
  virtual void OnNetworksChanged();

  // Signaled by |service| when it's been unloaded by Manager. Returns true if
  // this provider has released ownership of the service, and false otherwise.
  virtual bool OnServiceUnloaded(const WiMaxServiceRefPtr &service);

  // Selects and returns a WiMAX device to connect |service| through.
  virtual WiMaxRefPtr SelectCarrier(const WiMaxServiceConstRefPtr &service);

 private:
  friend class WiMaxProviderTest;
  FRIEND_TEST(WiMaxProviderTest, ConnectDisconnectWiMaxManager);
  FRIEND_TEST(WiMaxProviderTest, CreateDevice);
  FRIEND_TEST(WiMaxProviderTest, CreateServicesFromProfile);
  FRIEND_TEST(WiMaxProviderTest, DestroyAllServices);
  FRIEND_TEST(WiMaxProviderTest, DestroyDeadDevices);
  FRIEND_TEST(WiMaxProviderTest, FindService);
  FRIEND_TEST(WiMaxProviderTest, GetLinkName);
  FRIEND_TEST(WiMaxProviderTest, GetUniqueService);
  FRIEND_TEST(WiMaxProviderTest, OnDeviceInfoAvailable);
  FRIEND_TEST(WiMaxProviderTest, OnDevicesChanged);
  FRIEND_TEST(WiMaxProviderTest, OnNetworksChanged);
  FRIEND_TEST(WiMaxProviderTest, OnServiceUnloaded);
  FRIEND_TEST(WiMaxProviderTest, RetrieveNetworkInfo);
  FRIEND_TEST(WiMaxProviderTest, SelectCarrier);
  FRIEND_TEST(WiMaxProviderTest, StartLiveServices);
  FRIEND_TEST(WiMaxProviderTest, StartStop);
  FRIEND_TEST(WiMaxProviderTest, StopDeadServices);

  struct NetworkInfo {
    WiMaxNetworkId id;
    std::string name;
  };

  void ConnectToWiMaxManager();
  void DisconnectFromWiMaxManager();
  void OnWiMaxManagerAppear(const std::string &owner);

  void OnDevicesChanged(const RpcIdentifiers &devices);

  void CreateDevice(const std::string &link_name, const RpcIdentifier &path);
  void DestroyDeadDevices(const RpcIdentifiers &live_devices);

  std::string GetLinkName(const RpcIdentifier &path);

  // Retrieves network info for a network at RPC |path| into |networks_| if it's
  // not already available.
  void RetrieveNetworkInfo(const RpcIdentifier &path);

  // Finds and returns the service identified by |storage_id|. Returns NULL if
  // the service is not found.
  WiMaxServiceRefPtr FindService(const std::string &storage_id);

  // Finds or creates a service with the given parameters. The parameters
  // uniquely identify a service so no duplicate services will be created.
  WiMaxServiceRefPtr GetUniqueService(const WiMaxNetworkId &id,
                                      const std::string &name);

  // Starts all services with network ids in the current set of live
  // networks. This method also creates, registers and starts the default
  // service for each live network.
  void StartLiveServices();

  // Stops all services with network ids that are not in the current set of live
  // networks.
  void StopDeadServices();

  // Stops, deregisters and destroys all services.
  void DestroyAllServices();

  ControlInterface *control_;
  EventDispatcher *dispatcher_;
  Metrics *metrics_;
  Manager *manager_;

  // Monitor WiMaxManager DBus name ownership to detect daemon presence.
  DBusManager::CancelableAppearedCallback on_wimax_manager_appear_;
  DBusManager::CancelableVanishedCallback on_wimax_manager_vanish_;

  scoped_ptr<WiMaxManagerProxyInterface> wimax_manager_proxy_;

  // Key is the interface link name.
  std::map<std::string, RpcIdentifier> pending_devices_;
  std::map<std::string, WiMaxRefPtr> devices_;
  // Key is service's storage identifier.
  std::map<std::string, WiMaxServiceRefPtr> services_;
  std::map<RpcIdentifier, NetworkInfo> networks_;

  ProxyFactory *proxy_factory_;

  DISALLOW_COPY_AND_ASSIGN(WiMaxProvider);
};

}  // namespace shill

#endif  // SHILL_WIMAX_PROVIDER_H_
