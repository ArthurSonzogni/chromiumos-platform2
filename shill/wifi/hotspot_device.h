// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_HOTSPOT_DEVICE_H_
#define SHILL_WIFI_HOTSPOT_DEVICE_H_

#include <string>

#include <base/memory/weak_ptr.h>

#include "shill/refptr_types.h"
#include "shill/store/key_value_store.h"
#include "shill/supplicant/supplicant_event_delegate_interface.h"
#include "shill/wifi/local_device.h"

namespace shill {

class SupplicantInterfaceProxyInterface;

class HotspotDevice : public LocalDevice,
                      public SupplicantEventDelegateInterface {
 public:
  // Constructor function
  HotspotDevice(Manager* manager,
                const std::string& link_name,
                const std::string& mac_address,
                uint32_t phy_index,
                LocalDevice::EventCallback callback);

  HotspotDevice(const HotspotDevice&) = delete;
  HotspotDevice& operator=(const HotspotDevice&) = delete;

  ~HotspotDevice() override;

  // HotspotDevice start routine. Like connect to wpa_supplicant, register
  // netlink events, clean up any wpa_supplicant networks, etc. Return true if
  // interface is started successfully. Return false if error happens.
  bool Start() override;

  // HotspotDevice stop routine. Like clean up wpa_supplicant networks,
  // disconnect to wpa_supplicant, deregister netlink events, etc. Return true
  // if interface is stopped. Return false if fail to remove the wlan interface
  // but other resources have been cleaned up.
  bool Stop() override;

  // Implementation of SupplicantEventDelegateInterface.  These methods
  // are called by SupplicantInterfaceProxy, in response to events from
  // wpa_supplicant.
  void PropertiesChanged(const KeyValueStore& properties) override;
  void BSSAdded(const RpcIdentifier& BSS,
                const KeyValueStore& properties) override{};
  void BSSRemoved(const RpcIdentifier& BSS) override{};
  void Certification(const KeyValueStore& properties) override{};
  void EAPEvent(const std::string& status,
                const std::string& parameter) override{};
  void ScanDone(const bool& success) override{};
  void InterworkingAPAdded(const RpcIdentifier& BSS,
                           const RpcIdentifier& cred,
                           const KeyValueStore& properties) override{};
  void InterworkingSelectDone() override{};

 private:
  friend class HotspotDeviceTest;
  FRIEND_TEST(HotspotDeviceTest, InterfaceDisabled);

  // Create an AP interface and connect to the wpa_supplicant interface proxy.
  bool CreateInterface();
  // Remove the AP interface and disconnect from the wpa_supplicant interface
  // proxy.
  bool RemoveInterface();
  void PropertiesChangedTask(const KeyValueStore& properties);
  void StateChanged(const std::string& new_state);

  std::unique_ptr<SupplicantInterfaceProxyInterface>
      supplicant_interface_proxy_;
  // wpa_supplicant's RPC path for this device/interface.
  RpcIdentifier supplicant_interface_path_;
  std::string supplicant_state_;
  base::WeakPtrFactory<HotspotDevice> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_WIFI_HOTSPOT_DEVICE_H_
