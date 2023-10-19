// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback_list.h>
#include <dbus/object_path.h>

#include "diagnostics/cros_healthd/system/floss_callback_services.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxies.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxies.h"

namespace diagnostics {

// Supported Bluetooth property types, which is copied and modified from
// |BtPropertyType| enum in the Android codebase:
// packages/modules/Bluetooth/system/gd/rust/topshim/src/btif.rs
enum class BtPropertyType : uint32_t {
  kBdName = 0x1,
  kBdAddr,
  kUuids,
  kClassOfDevice,
  kTypeOfDevice,
  kServiceRecord,
  kAdapterScanMode,
  kAdapterBondedDevices,
  kAdapterDiscoverableTimeout,
  kRemoteFriendlyName,
  kRemoteRssi,
  kRemoteVersionInfo,
  kLocalLeFeatures,
  kLocalIoCaps,
  kLocalIoCapsBle,
  kDynamicAudioBuffer,
  kRemoteIsCoordinatedSetMember,
  kAppearance,
  kVendorProductInfo = 0x13,
  // Unimplemented:
  //  BT_PROPERTY_WL_MEDIA_PLAYERS_LIST,
  //  BT_PROPERTY_REMOTE_ASHA_CAPABILITY,
  //  BT_PROPERTY_REMOTE_ASHA_TRUNCATED_HISYNCID,
  //  BT_PROPERTY_REMOTE_MODEL_NUM,
  kRemoteAddrType = 0x18,

  kUnknown = 0xFE,
  kRemoteDeviceTimestamp = 0xFF,
};

// Bluetooth device bond state, which is copied and modified from |BondState|
// enum in the Android codebase:
// packages/modules/Bluetooth/system/gd/rust/topshim/src/btif.rs
enum class BondState : uint32_t {
  kNotBonded = 0,
  kBondingInProgress = 1,
  kBonded = 2,
};

// Adapter events.
using OnFlossAdapterAddedCallback = base::RepeatingCallback<void(
    org::chromium::bluetooth::BluetoothProxyInterface* adapter)>;
using OnFlossAdapterRemovedCallback =
    base::RepeatingCallback<void(const dbus::ObjectPath& adapter_path)>;
using OnFlossAdapterPropertyChangedCallback = base::RepeatingCallback<void(
    const dbus::ObjectPath& adapter_path, BtPropertyType property)>;
using OnFlossAdapterPoweredChangedCallback =
    base::RepeatingCallback<void(int32_t hci_interface, bool powered)>;
using OnFlossAdapterDiscoveringChangedCallback = base::RepeatingCallback<void(
    const dbus::ObjectPath& adapter_path, bool discovering)>;
// Device events.
using OnFlossDeviceAddedCallback =
    base::RepeatingCallback<void(const brillo::VariantDictionary& device)>;
using OnFlossDeviceRemovedCallback =
    base::RepeatingCallback<void(const brillo::VariantDictionary& device)>;
using OnFlossDevicePropertyChangedCallback = base::RepeatingCallback<void(
    const brillo::VariantDictionary& device, BtPropertyType property)>;
using OnFlossDeviceConnectedChangedCallback = base::RepeatingCallback<void(
    const brillo::VariantDictionary& device, bool connected)>;
using OnFlossDeviceBondChangedCallback = base::RepeatingCallback<void(
    uint32_t bt_status, const std::string& address, BondState bond_state)>;
using OnFlossDeviceSspRequestCallback =
    base::RepeatingCallback<void(const brillo::VariantDictionary& device)>;
// Other floss events.
using OnFlossManagerRemovedCallback =
    base::RepeatingCallback<void(const dbus::ObjectPath& manager_path)>;
using OnFlossScanResultReceivedCallback =
    base::RepeatingCallback<void(const brillo::VariantDictionary& scan_result)>;

// Interface for subscribing Bluetooth events via Floss proxies.
class FlossEventHub {
 public:
  explicit FlossEventHub(
      const scoped_refptr<dbus::Bus>& bus = nullptr,
      org::chromium::bluetooth::Manager::ObjectManagerProxy*
          bluetooth_manager_proxy = nullptr,
      org::chromium::bluetooth::ObjectManagerProxy* bluetooth_proxy = nullptr);
  FlossEventHub(const FlossEventHub&) = delete;
  FlossEventHub& operator=(const FlossEventHub&) = delete;
  ~FlossEventHub();

  base::CallbackListSubscription SubscribeAdapterAdded(
      OnFlossAdapterAddedCallback callback);
  base::CallbackListSubscription SubscribeAdapterRemoved(
      OnFlossAdapterRemovedCallback callback);
  base::CallbackListSubscription SubscribeAdapterPoweredChanged(
      OnFlossAdapterPoweredChangedCallback callback);
  base::CallbackListSubscription SubscribeAdapterPropertyChanged(
      OnFlossAdapterPropertyChangedCallback callback);
  base::CallbackListSubscription SubscribeAdapterDiscoveringChanged(
      OnFlossAdapterDiscoveringChangedCallback callback);
  base::CallbackListSubscription SubscribeDeviceAdded(
      OnFlossDeviceAddedCallback callback);
  base::CallbackListSubscription SubscribeDeviceRemoved(
      OnFlossDeviceRemovedCallback callback);
  base::CallbackListSubscription SubscribeDevicePropertyChanged(
      OnFlossDevicePropertyChangedCallback callback);
  base::CallbackListSubscription SubscribeDeviceConnectedChanged(
      OnFlossDeviceConnectedChangedCallback callback);
  base::CallbackListSubscription SubscribeDeviceBondChanged(
      OnFlossDeviceBondChangedCallback callback);
  base::CallbackListSubscription SubscribeDeviceSspRequest(
      OnFlossDeviceSspRequestCallback callback);
  base::CallbackListSubscription SubscribeManagerRemoved(
      OnFlossManagerRemovedCallback callback);
  base::CallbackListSubscription SubscribeScanResultReceived(
      OnFlossScanResultReceivedCallback callback);

 protected:
  // Interfaces for subclass to send events.
  void OnManagerAdded(org::chromium::bluetooth::ManagerProxyInterface* adapter);
  void OnManagerRemoved(const dbus::ObjectPath& adapter_path);
  void OnAdapterAdded(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter);
  void OnAdapterRemoved(const dbus::ObjectPath& adapter_path);
  void OnAdapterGattAdded(
      org::chromium::bluetooth::BluetoothGattProxyInterface* adapter);
  void OnAdapterGattRemoved(const dbus::ObjectPath& adapter_path);

  friend class BluetoothCallbackService;
  friend class ManagerCallbackService;
  friend class BluetoothConnectionCallbackService;
  friend class ScannerCallbackService;

  // Interfaces for CallbackService to send events.
  void OnAdapterPropertyChanged(const dbus::ObjectPath& adapter_path,
                                uint32_t property);
  void OnAdapterPoweredChanged(int32_t hci_interface, bool powered);
  void OnAdapterDiscoveringChanged(const dbus::ObjectPath& adapter_path,
                                   bool discovering);
  void OnDeviceAdded(const brillo::VariantDictionary& device);
  void OnDeviceRemoved(const brillo::VariantDictionary& device);
  void OnDevicePropertiesChanged(const brillo::VariantDictionary& device,
                                 const std::vector<uint32_t>& properties);
  void OnDeviceConnectedChanged(const brillo::VariantDictionary& device,
                                bool connected);
  void OnDeviceBondChanged(uint32_t bt_status,
                           const std::string& address,
                           uint32_t bond_state);
  void OnDeviceSspRequest(const brillo::VariantDictionary& device);
  void OnScanResultReceived(const brillo::VariantDictionary& scan_result);

 private:
  // Get the unique object path for BluetoothProxy callback services.
  dbus::ObjectPath GetNextBluetoothCallbackPath();

  void HandleRegisterBluetoothCallbackResponse(
      const dbus::ObjectPath& adapter_path,
      const dbus::ObjectPath& callback_path,
      brillo::Error* error,
      uint32_t register_id);

  void HandleRegisterManagerCallbackResponse(
      const dbus::ObjectPath& callback_path, brillo::Error* error);

  void HandleRegisterConnectionCallbackResponse(
      const dbus::ObjectPath& adapter_path,
      const dbus::ObjectPath& callback_path,
      brillo::Error* error,
      uint32_t register_id);

  void HandleRegisterScannerCallbackResponse(
      const dbus::ObjectPath& adapter_path,
      const dbus::ObjectPath& callback_path,
      brillo::Error* error,
      uint32_t register_id);

  // Observer callback list.
  base::RepeatingCallbackList<void(
      org::chromium::bluetooth::BluetoothProxyInterface* adapter)>
      adapter_added_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath& adapter_path)>
      adapter_removed_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath& adapter_path,
                                   BtPropertyType property)>
      adapter_property_changed_observers_;
  base::RepeatingCallbackList<void(int32_t, bool)>
      adapter_powered_changed_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath&, bool)>
      adapter_discovering_changed_observers_;
  base::RepeatingCallbackList<void(const brillo::VariantDictionary& device)>
      device_added_observers_;
  base::RepeatingCallbackList<void(const brillo::VariantDictionary& device)>
      device_removed_observers_;
  base::RepeatingCallbackList<void(const brillo::VariantDictionary& device,
                                   BtPropertyType property)>
      device_property_changed_observers_;
  base::RepeatingCallbackList<void(const brillo::VariantDictionary& device,
                                   bool connected)>
      device_connected_changed_observers_;
  base::RepeatingCallbackList<void(
      uint32_t bt_status, const std::string& address, BondState bond_state)>
      device_bond_changed_observers_;
  base::RepeatingCallbackList<void(const brillo::VariantDictionary& device)>
      device_ssp_request_observers_;
  base::RepeatingCallbackList<void(const dbus::ObjectPath& adapter_path)>
      manager_removed_observers_;
  base::RepeatingCallbackList<void(
      const brillo::VariantDictionary& scan_result)>
      scan_result_received_observers_;

  // Used to create Floss callback services.
  scoped_refptr<dbus::Bus> bus_;

  // Callback services.
  std::unique_ptr<ManagerCallbackService> manager_callback_;
  std::map<dbus::ObjectPath, std::unique_ptr<BluetoothCallbackService>>
      adapter_callbacks_;
  std::map<dbus::ObjectPath,
           std::unique_ptr<BluetoothConnectionCallbackService>>
      connection_callbacks_;
  std::map<dbus::ObjectPath, std::unique_ptr<ScannerCallbackService>>
      scanner_callbacks_;

  // The next index used to create callback service.
  uint32_t callback_path_index_ = 0;

  // Must be the last class member.
  base::WeakPtrFactory<FlossEventHub> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FLOSS_EVENT_HUB_H_
