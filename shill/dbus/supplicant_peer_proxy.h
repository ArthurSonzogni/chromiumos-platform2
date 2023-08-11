// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_SUPPLICANT_PEER_PROXY_H_
#define SHILL_DBUS_SUPPLICANT_PEER_PROXY_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>

#include "shill/store/key_value_store.h"
#include "shill/supplicant/supplicant_peer_proxy_interface.h"
#include "supplicant/dbus-proxies.h"

namespace shill {

class SupplicantPeerProxy : public SupplicantPeerProxyInterface {
 public:
  SupplicantPeerProxy(const scoped_refptr<dbus::Bus>& bus,
                      const RpcIdentifier& object_path);
  SupplicantPeerProxy(const SupplicantPeerProxy&) = delete;
  SupplicantPeerProxy& operator=(const SupplicantPeerProxy&) = delete;

  ~SupplicantPeerProxy() override;

 private:
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const std::string& interface_name,
                const PropertyChangedCallback& callback);
    PropertySet(const PropertySet&) = delete;
    PropertySet& operator=(const PropertySet&) = delete;

    brillo::dbus_utils::Property<std::string> device_name;
    brillo::dbus_utils::Property<uint8_t> device_cap;
    brillo::dbus_utils::Property<uint8_t> group_cap;
    brillo::dbus_utils::Property<std::vector<uint8_t>> device_address;

   private:
  };

  static constexpr char kInterfaceName[] = "fi.w1.wpa_supplicant1.Peer";
  static constexpr char kPropertyDeviceName[] = "DeviceName";
  static constexpr char kPropertyDeviceCap[] = "devicecapability";
  static constexpr char kPropertyGroupCap[] = "groupcapability";
  static constexpr char kPropertyDeviceAddress[] = "DeviceAddress";

  // Signal handlers.
  void PropertiesChanged(const brillo::VariantDictionary& properties);

  // Callback invoked when the value of property |property_name| is changed.
  void OnPropertyChanged(const std::string& property_name);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  std::unique_ptr<fi::w1::wpa_supplicant1::PeerProxy> peer_proxy_;
  std::unique_ptr<PropertySet> properties_;

  base::WeakPtrFactory<SupplicantPeerProxy> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_SUPPLICANT_PEER_PROXY_H_
