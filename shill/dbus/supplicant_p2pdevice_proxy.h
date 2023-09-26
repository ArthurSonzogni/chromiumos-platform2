// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_SUPPLICANT_P2PDEVICE_PROXY_H_
#define SHILL_DBUS_SUPPLICANT_P2PDEVICE_PROXY_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>

#include "shill/supplicant/supplicant_p2pdevice_proxy_interface.h"
#include "supplicant/dbus-proxies.h"

namespace shill {

class SupplicantP2PDeviceEventDelegateInterface;

class SupplicantP2PDeviceProxy : public SupplicantP2PDeviceProxyInterface {
 public:
  SupplicantP2PDeviceProxy(const scoped_refptr<dbus::Bus>& bus,
                           const RpcIdentifier& object_path,
                           SupplicantP2PDeviceEventDelegateInterface* delegate);
  SupplicantP2PDeviceProxy(const SupplicantP2PDeviceProxy&) = delete;
  SupplicantP2PDeviceProxy& operator=(const SupplicantP2PDeviceProxy&) = delete;

  ~SupplicantP2PDeviceProxy() override;

  // Implementation of SupplicantP2PDeviceProxyInterface.
  bool GroupAdd(const KeyValueStore& args) override;
  bool Disconnect() override;
  bool AddPersistentGroup(const KeyValueStore& args,
                          RpcIdentifier* rpc_identifier) override;
  bool RemovePersistentGroup(const RpcIdentifier& rpc_identifier) override;

  bool GetDeviceConfig(KeyValueStore* config) override;

 private:
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const std::string& interface_name,
                const PropertyChangedCallback& callback);
    PropertySet(const PropertySet&) = delete;
    PropertySet& operator=(const PropertySet&) = delete;

    brillo::dbus_utils::Property<brillo::VariantDictionary> device_config;

   private:
  };

  static constexpr char kInterfaceName[] =
      "fi.w1.wpa_supplicant1.Interface.P2PDevice";
  static constexpr char kPropertyDeviceConfig[] = "P2PDeviceConfig";

  // Signal handlers.
  void GroupStarted(const brillo::VariantDictionary& properties);
  void GroupFinished(const brillo::VariantDictionary& properties);
  void GroupFormationFailure(const std::string& reason);

  // Callback invoked when the value of property |property_name| is changed.
  void OnPropertyChanged(const std::string& property_name);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  std::unique_ptr<fi::w1::wpa_supplicant1::Interface::P2PDeviceProxy>
      p2pdevice_proxy_;
  std::unique_ptr<PropertySet> properties_;

  // This pointer is owned by the object that created |this|.  That object
  // MUST destroy |this| before destroying itself.
  SupplicantP2PDeviceEventDelegateInterface* delegate_;

  base::WeakPtrFactory<SupplicantP2PDeviceProxy> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_SUPPLICANT_P2PDEVICE_PROXY_H_
