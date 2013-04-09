// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIMAX_SERVICE_H_
#define SHILL_WIMAX_SERVICE_H_

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/refptr_types.h"
#include "shill/service.h"
#include "shill/wimax_network_proxy_interface.h"

namespace shill {

class KeyValueStore;

class WiMaxService : public Service {
 public:
  static const char kStorageNetworkId[];

  // TODO(petkov): Declare this in chromeos/dbus/service_constants.h.
  static const char kNetworkIdProperty[];

  WiMaxService(ControlInterface *control,
               EventDispatcher *dispatcher,
               Metrics *metrics,
               Manager *manager);
  virtual ~WiMaxService();

  // Returns the parameters to be passed to WiMaxManager.Device.Connect() when
  // connecting to the network associated with this service.
  void GetConnectParameters(KeyValueStore *parameters) const;

  // Returns the RPC object path for the WiMaxManager.Network object associated
  // with this service. Must only be called after |proxy_| is set by Start().
  virtual RpcIdentifier GetNetworkObjectPath() const;

  // Starts the service by associating it with the RPC network object |proxy|
  // and listening for its signal strength. Returns true on success, false
  // otherwise. Takes ownership of proxy, regardless of the result of the
  // operation. The proxy will be destroyed on failure.
  virtual bool Start(WiMaxNetworkProxyInterface *proxy);

  // Stops the service by disassociating it from |proxy_| and resetting its
  // signal strength to 0. If the service is connected, it notifies the carrier
  // device that the service is stopped.
  virtual void Stop();

  virtual bool IsStarted() const;

  const std::string &network_name() const { return network_name_; }
  const WiMaxNetworkId &network_id() const { return network_id_; }
  void set_network_id(const WiMaxNetworkId &id) { network_id_ = id; }
  bool is_default() const { return is_default_; }
  void set_is_default(bool is_default) { is_default_ = is_default; }

  static WiMaxNetworkId ConvertIdentifierToNetworkId(uint32 identifier);

  // Initializes the storage identifier. Note that the friendly service name and
  // the |network_id_| must already be initialized.
  void InitStorageIdentifier();
  static std::string CreateStorageIdentifier(const WiMaxNetworkId &id,
                                             const std::string &name);

  virtual void ClearPassphrase();

  // Inherited from Service.
  virtual void Connect(Error *error, const char *reason);
  virtual void Disconnect(Error *error);
  virtual std::string GetStorageIdentifier() const;
  virtual bool Is8021x() const;
  virtual void set_eap(const EapCredentials &eap);
  virtual bool Save(StoreInterface *storage);
  virtual bool Unload();
  virtual void SetState(ConnectState state);

 private:
  friend class WiMaxServiceTest;
  FRIEND_TEST(WiMaxServiceTest, GetDeviceRpcId);
  FRIEND_TEST(WiMaxServiceTest, IsAutoConnectable);
  FRIEND_TEST(WiMaxServiceTest, OnSignalStrengthChanged);
  FRIEND_TEST(WiMaxServiceTest, SetEAP);
  FRIEND_TEST(WiMaxServiceTest, SetState);
  FRIEND_TEST(WiMaxServiceTest, StartStop);

  // Inherited from Service.
  virtual std::string GetDeviceRpcId(Error *error);
  virtual bool IsAutoConnectable(const char **reason) const;

  void OnSignalStrengthChanged(int strength);

  void UpdateConnectable();

  WiMaxRefPtr device_;
  scoped_ptr<WiMaxNetworkProxyInterface> proxy_;
  std::string storage_id_;

  WiMaxNetworkId network_id_;
  std::string network_name_;
  bool need_passphrase_;
  bool is_default_;

  DISALLOW_COPY_AND_ASSIGN(WiMaxService);
};

}  // namespace shill

#endif  // SHILL_WIMAX_SERVICE_H_
