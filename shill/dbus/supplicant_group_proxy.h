// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_SUPPLICANT_GROUP_PROXY_H_
#define SHILL_DBUS_SUPPLICANT_GROUP_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "shill/store/key_value_store.h"
#include "shill/supplicant/supplicant_group_proxy_interface.h"
#include "supplicant/dbus-proxies.h"

namespace shill {

class SupplicantGroupEventDelegateInterface;

class SupplicantGroupProxy : public SupplicantGroupProxyInterface {
 public:
  SupplicantGroupProxy(const scoped_refptr<dbus::Bus>& bus,
                       const RpcIdentifier& object_path,
                       SupplicantGroupEventDelegateInterface* delegate);
  SupplicantGroupProxy(const SupplicantGroupProxy&) = delete;
  SupplicantGroupProxy& operator=(const SupplicantGroupProxy&) = delete;

  ~SupplicantGroupProxy() override;

 private:
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const std::string& interface_name,
                const PropertyChangedCallback& callback);
    PropertySet(const PropertySet&) = delete;
    PropertySet& operator=(const PropertySet&) = delete;

    brillo::dbus_utils::Property<std::vector<dbus::ObjectPath>> members;
    brillo::dbus_utils::Property<std::string> role;
    brillo::dbus_utils::Property<std::vector<uint8_t>> ssid;
    brillo::dbus_utils::Property<std::vector<uint8_t>> bssid;
    brillo::dbus_utils::Property<uint16_t> frequency;
    brillo::dbus_utils::Property<std::string> passphrase;

   private:
  };

  static constexpr char kInterfaceName[] = "fi.w1.wpa_supplicant1.Group";
  static constexpr char kPropertyMembers[] = "Members";
  static constexpr char kPropertyRole[] = "Role";
  static constexpr char kPropertySSID[] = "SSID";
  static constexpr char kPropertyBSSID[] = "BSSID";
  static constexpr char kPropertyFrequency[] = "Frequency";
  static constexpr char kPropertyPassphrase[] = "Passphrase";

  // Signal handlers.
  void PeerJoined(const dbus::ObjectPath& peer);
  void PeerDisconnected(const dbus::ObjectPath& peer);

  // Callback invoked when the value of property |property_name| is changed.
  void OnPropertyChanged(const std::string& property_name);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  std::unique_ptr<fi::w1::wpa_supplicant1::GroupProxy> group_proxy_;
  std::unique_ptr<PropertySet> properties_;

  // This pointer is owned by the object that created |this|. That object
  // MUST destroy |this| before destroying itself.
  SupplicantGroupEventDelegateInterface* delegate_;

  base::WeakPtrFactory<SupplicantGroupProxy> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_SUPPLICANT_GROUP_PROXY_H_
